/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/ssvg/ssvg.h"

#include <cmath>
#include <string>

#include "gfx/command_helper.h"
#include "gfx/rendering/global_render_resource.h"
#include "gfx/rtg/graph_builder.h"
#include "gfx/rtg/rtg_common.h"
#include "resource/resource_manager.h"

namespace ngl::render::app
{
    // シェーダ側と一致させる.
    // CoarseVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_per_voxel_data_u32_count (1)
    // CoarseVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_per_voxel_occupancy_reso (8)
    #define k_per_voxel_occupancy_bit_count (k_per_voxel_occupancy_reso*k_per_voxel_occupancy_reso*k_per_voxel_occupancy_reso)
    #define k_per_voxel_occupancy_u32_count ((k_per_voxel_occupancy_bit_count + 31) / 32)

    // CoarseVoxel単位のデータサイズ(u32単位)
    #define k_per_voxel_u32_count (k_per_voxel_occupancy_u32_count + k_per_voxel_data_u32_count)

    SsVg::~SsVg()
    {
    }

    // 初期化
    bool SsVg::Initialize(ngl::rhi::DeviceDep* p_device)
    {
        // Helper function to create compute shader PSO
        auto CreateComputePSO = [&](const char* shader_path) -> ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>
        {
            auto pso                                          = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
            ngl::rhi::ComputePipelineStateDep::Desc cpso_desc = {};
            {
                ngl::gfx::ResShader::LoadDesc cs_load_desc = {};
                cs_load_desc.stage                         = ngl::rhi::EShaderStage::Compute;
                cs_load_desc.shader_model_version          = k_shader_model;
                cs_load_desc.entry_point_name              = "main_cs";
                auto cs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                    p_device, NGL_RENDER_SHADER_PATH(shader_path), &cs_load_desc);
                cpso_desc.cs = &cs_load_handle->data_;
            }
            pso->Initialize(p_device, cpso_desc);
            return pso;
        };
        {
            pso_clear_voxel_  = CreateComputePSO("ssvg/clear_voxel_cs.hlsl");
            pso_begin_update_ = CreateComputePSO("ssvg/begin_update_cs.hlsl");
            pso_voxelize_     = CreateComputePSO("ssvg/voxelize_pass_cs.hlsl");
            pso_coarse_voxel_update_ = CreateComputePSO("ssvg/coarse_voxel_update_cs.hlsl");

            pso_debug_visualize_ = CreateComputePSO("ssvg/voxel_debug_visualize_cs.hlsl");
        }

        {
            work_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = 4,
                                               .element_count     = base_resolution_.x * base_resolution_.y * base_resolution_.z,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            const u32 voxel_count = base_resolution_.x * base_resolution_.y * base_resolution_.z;
            occupancy_bitmask_voxel_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = 4,
                                               .element_count     = voxel_count * k_per_voxel_u32_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }

        return true;
    }

    void SsVg::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        important_point_ = pos;
        important_dir_   = dir;
    }

    void SsVg::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "SsVg");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const bool is_first_dispatch = is_first_dispatch_;
        is_first_dispatch_           = false;


        // 重視位置を若干補正.
        #if 0
            const math::Vec3 modified_important_point = important_point_ + important_dir_ * 5.0f;
        #else
            const math::Vec3 modified_important_point = important_point_;
        #endif
        {
            // 中心を離散CELLIDで保持.
            grid_center_cell_id_prev_ = grid_center_cell_id_;
            grid_center_cell_id_      = (modified_important_point / cell_size_).Cast<int>();

            // 離散CELLIDからGridMin情報を復元.
            grid_min_pos_prev_ = grid_center_cell_id_prev_.Cast<float>() * cell_size_ - base_resolution_.Cast<float>() * 0.5f * cell_size_;
            grid_min_pos_      = grid_center_cell_id_.Cast<float>() * cell_size_ - base_resolution_.Cast<float>() * 0.5f * cell_size_;
        }
        math::Vec3i grid_min_pos_delta_cell = grid_center_cell_id_ - grid_center_cell_id_prev_;

        grid_toroidal_offset_prev_ = grid_toroidal_offset_;
        // シフトコピーをせずにToroidalにアクセスするためのオフセット. このオフセットをした後に mod を取った位置にアクセスする. その外側はInvalidateされる.
        grid_toroidal_offset_ = (((grid_toroidal_offset_ + grid_min_pos_delta_cell) % base_resolution_.Cast<int>()) + base_resolution_.Cast<int>()) % base_resolution_.Cast<int>();

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));

        const u32 voxel_count = base_resolution_.x * base_resolution_.y * base_resolution_.z;
        struct DispatchParam
        {
            math::Vec3i BaseResolution;
            u32 Flag;

            math::Vec3 GridMinPos;
            float CellSize;
            math::Vec3i GridToroidalOffset;
            float CellSizeInv;

            math::Vec3i GridToroidalOffsetPrev;
            int Dummy0;
            
            math::Vec3i GridCellDelta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
            int Dummy1;

            math::Vec2i TexHardwareDepthSize;

        };
        auto cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(DispatchParam));
        {
            auto* p = cbh->buffer_.MapAs<DispatchParam>();

            p->BaseResolution = base_resolution_.Cast<int>();
            p->Flag           = 0;

            p->GridMinPos     = grid_min_pos_;
            
            p->GridToroidalOffset = grid_toroidal_offset_;
            p->GridToroidalOffsetPrev = grid_toroidal_offset_prev_;

            p->GridCellDelta = grid_min_pos_delta_cell;

            p->CellSize       = cell_size_;
            p->CellSizeInv    = 1.0f / cell_size_;

            p->TexHardwareDepthSize = hw_depth_size;

            cbh->buffer_.Unmap();
        }

        if (is_first_dispatch)
        {
            // 初回クリア.
            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_clear_voxel_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);
            pso_clear_voxel_->SetView(&desc_set, "RWBufferWork", work_buffer_.uav.Get());
            pso_clear_voxel_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());

            p_command_list->SetPipelineState(pso_clear_voxel_.Get());
            p_command_list->SetDescriptorSet(pso_clear_voxel_.Get(), &desc_set);
            pso_clear_voxel_->DispatchHelper(p_command_list, voxel_count, 1, 1);  // Voxel Dispatch.

            p_command_list->ResourceUavBarrier(work_buffer_.buffer.Get());
            p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
        }
        // Begin Update Pass.
        {
            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_begin_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_begin_update_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);
            pso_begin_update_->SetView(&desc_set, "RWBufferWork", work_buffer_.uav.Get());
            pso_begin_update_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());

            p_command_list->SetPipelineState(pso_begin_update_.Get());
            p_command_list->SetDescriptorSet(pso_begin_update_.Get(), &desc_set);
            pso_begin_update_->DispatchHelper(p_command_list, voxel_count, 1, 1);  // Screen処理でDispatch.

            p_command_list->ResourceUavBarrier(work_buffer_.buffer.Get());
            p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
        }
        // Voxelization Pass.
        {
            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_voxelize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_voxelize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_voxelize_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);
            pso_voxelize_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());

            p_command_list->SetPipelineState(pso_voxelize_.Get());
            p_command_list->SetDescriptorSet(pso_voxelize_.Get(), &desc_set);
            pso_voxelize_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

            p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
        }
        // Coarse Voxel Update Pass.
        {
            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_coarse_voxel_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_coarse_voxel_update_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);
            pso_coarse_voxel_update_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());

            p_command_list->SetPipelineState(pso_coarse_voxel_update_.Get());
            p_command_list->SetDescriptorSet(pso_coarse_voxel_update_.Get(), &desc_set);
            pso_coarse_voxel_update_->DispatchHelper(p_command_list, voxel_count, 1, 1);  // Screen処理でDispatch.

            p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
        }

        // デバッグ描画.
        {
            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_debug_visualize_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);
            pso_debug_visualize_->SetView(&desc_set, "BufferWork", work_buffer_.srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "OccupancyBitmaskVoxel", occupancy_bitmask_voxel_.srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "RWTexWork", work_uav.Get());

            p_command_list->SetPipelineState(pso_debug_visualize_.Get());
            p_command_list->SetDescriptorSet(pso_debug_visualize_.Get(), &desc_set);

            pso_debug_visualize_->DispatchHelper(p_command_list, work_tex_size.x, work_tex_size.y, 1);
        }
    }

}  // namespace ngl::render::app
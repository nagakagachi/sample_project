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
    // シェーダとCppで一致させる.
    // ObmVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_obm_common_data_u32_count (1)
    // ObmVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_obm_per_voxel_resolution (8)
    #define k_obm_per_voxel_bitmask_bit_count (k_obm_per_voxel_resolution*k_obm_per_voxel_resolution*k_obm_per_voxel_resolution)
    #define k_obm_per_voxel_occupancy_bitmask_u32_count ((k_obm_per_voxel_bitmask_bit_count + 31) / 32)
    // ObmVoxel単位のデータサイズ(u32単位)
    #define k_obm_per_voxel_u32_count (k_obm_per_voxel_occupancy_bitmask_u32_count + k_obm_common_data_u32_count)

    #define k_obm_per_voxel_resolution_inv (1.0 / float(k_obm_per_voxel_resolution))
    #define k_obm_per_voxel_resolution_vec3i int3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution)

    // probeあたりのOctMap解像度.
    #define k_probe_octmap_width (6)
    // それぞれのOctMapの+側境界に1テクセルボーダーを追加することで全方向に1テクセルのマージンを確保する.
    #define k_probe_octmap_width_with_border (k_probe_octmap_width+2)

    #define k_per_probe_texel_count (k_probe_octmap_width*k_probe_octmap_width)


    // 可視Probe更新時のスキップ数. 0でスキップせずに可視Probeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT 0
    // Probe全体更新のスキップ数. 0でスキップせずにProbeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_ALL_PROBE_SKIP_COUNT 60


    // シェーダとCppで一致させる.
    // CoarseVoxelバッファ. ObmVoxel一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct CoarseVoxelData
    {
        u32 probe_pos_index;   // ObmVoxel内部でのプローブ位置インデックス. 0は無効, probe_pos_index-1 が実際のインデックス. 値域は 0,k_obm_per_voxel_bitmask_bit_count.
        u32 reserved;          // 予備.
    };

    
    static constexpr size_t k_sizeof_CoarseVoxelData = sizeof(CoarseVoxelData);

    static constexpr u32 k_max_update_probe_work_count = 1024;



    // デバッグ.
    bool SsVg::dbg_view_enable_ = false;
    int SsVg::dbg_view_mode_ = 0;
    int SsVg::dbg_probe_debug_view_mode_ = -1;
    int SsVg::dbg_raytrace_version_ = 0;
    float SsVg::dbg_probe_scale_ = 1.0f;
    float SsVg::dbg_probe_near_geom_scale_ = 0.2f;
    




    SsVgCascade::~SsVgCascade()
    {
    }

    // 初期化
    bool SsVgCascade::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u base_resolution, float cell_size)
    {
        base_resolution_ = base_resolution;
        cell_size_ = cell_size;

        const u32 voxel_count = base_resolution_.x * base_resolution_.y * base_resolution_.z;
        probe_atlas_texture_base_width_ = static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(voxel_count))));

        update_probe_work_count_= std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);
        //update_probe_work_count_= 1;

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
            pso_coarse_probe_update_ = CreateComputePSO("ssvg/coarse_probe_update_cs.hlsl");
            pso_visible_probe_update_ = CreateComputePSO("ssvg/visible_probe_update_cs.hlsl");
            pso_visible_probe_post_update_ = CreateComputePSO("ssvg/visible_probe_post_update_cs.hlsl");
            pso_generate_visible_voxel_indirect_arg_ = CreateComputePSO("ssvg/generate_visible_voxel_indirect_arg_cs.hlsl");

            pso_coarse_voxel_update_old_ = CreateComputePSO("ssvg/coarse_voxel_update_old_cs.hlsl");// 旧バージョン検証.
            pso_debug_visualize_ = CreateComputePSO("ssvg/debug_util/voxel_debug_visualize_cs.hlsl");
            
            {
                pso_debug_obm_voxel_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                {
                    ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                    vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                    vs_load_desc.shader_model_version          = k_shader_model;
                    vs_load_desc.entry_point_name              = "main_vs";
                    auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                        p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/obm_voxel_debug_vs.hlsl"), &vs_load_desc);
                    gpso_desc.vs = &vs_load_handle->data_;
                }
                {
                    ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                    ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                    ps_load_desc.shader_model_version          = k_shader_model;
                    ps_load_desc.entry_point_name              = "main_ps";
                    auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                        p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/obm_voxel_debug_ps.hlsl"), &ps_load_desc);
                    gpso_desc.ps = &ps_load_handle->data_;
                }

                gpso_desc.num_render_targets = 1;
                gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                gpso_desc.depth_stencil_state.depth_enable = true;
                gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                gpso_desc.depth_stencil_state.depth_write_enable = true;
                gpso_desc.depth_stencil_state.stencil_enable = false;
                gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;
                
                if(!pso_debug_obm_voxel_->Initialize(p_device, gpso_desc))
                {
                    return false;
                }
            }
        }


        {
            coarse_voxel_data_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(CoarseVoxelData),
                                               .element_count     = base_resolution_.x * base_resolution_.y * base_resolution_.z,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default});
        }
        {
            occupancy_bitmask_voxel_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = voxel_count * k_obm_per_voxel_u32_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            visible_voxel_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = update_probe_work_count_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            visible_voxel_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            // 1F更新可能プローブ数分の k_probe_octmap_width*k_probe_octmap_width テクセル分バッファ.
            visible_voxel_update_probe_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(float),
                                               .element_count     = update_probe_work_count_ * (k_probe_octmap_width*k_probe_octmap_width),

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_FLOAT);
        }


        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = probe_atlas_texture_base_width_ * k_probe_octmap_width_with_border;
            desc.height = static_cast<u32>(std::ceil((voxel_count + probe_atlas_texture_base_width_-1) / probe_atlas_texture_base_width_)) * k_probe_octmap_width_with_border;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16_FLOAT;
            //desc.format = rhi::EResourceFormat::Format_R8_UNORM;
            //desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::UnorderedAccess;

            probe_skyvisibility_.Initialize(p_device, desc);
        }

        return true;
    }

    void SsVgCascade::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        important_point_ = pos;
        important_dir_   = dir;
    }

    void SsVgCascade::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "SsVg");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const bool is_first_dispatch = is_first_dispatch_;
        is_first_dispatch_           = false;
        ++frame_count_;


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
        struct SsvgParam
        {
            math::Vec3i base_grid_resolution{};
            u32 flag{};


            math::Vec3 grid_min_pos;
            float cell_size;
            math::Vec3i grid_toroidal_offset;
            float cell_size_inv;

            math::Vec3i grid_toroidal_offset_prev;
            int dummy0;
            
            math::Vec3i grid_move_cell_delta;// Toroidalではなくワールド空間Cellでのフレーム移動量.

            int probe_atlas_texture_base_width;

            math::Vec3i voxel_dispatch_thread_group_count;// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.

            int update_probe_work_count;// 更新プローブ用のワークサイズ.

            math::Vec2i tex_hw_depth_size;
            u32 frame_count;

            int debug_view_mode;
            int debug_probe_mode;
            float debug_probe_radius;
            float debug_probe_near_geom_scale;
        };

        cbh_dispatch_ = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SsvgParam));
        {
            auto* p = cbh_dispatch_->buffer_.MapAs<SsvgParam>();

            p->base_grid_resolution = base_resolution_.Cast<int>();
            p->flag           = 0;

            p->grid_min_pos     = grid_min_pos_;

            p->grid_toroidal_offset = grid_toroidal_offset_;
            p->grid_toroidal_offset_prev = grid_toroidal_offset_prev_;

            p->grid_move_cell_delta = grid_min_pos_delta_cell;

            p->probe_atlas_texture_base_width = probe_atlas_texture_base_width_;

            p->cell_size       = cell_size_;
            p->cell_size_inv    = 1.0f / cell_size_;

            p->voxel_dispatch_thread_group_count = math::Vec3i(pso_visible_probe_update_->GetThreadGroupSizeX(), pso_visible_probe_update_->GetThreadGroupSizeY(), pso_visible_probe_update_->GetThreadGroupSizeZ());

            p->update_probe_work_count = update_probe_work_count_;

            p->tex_hw_depth_size = hw_depth_size;

            p->frame_count = frame_count_;

            p->debug_view_mode = SsVg::dbg_view_mode_;
            p->debug_probe_mode = SsVg::dbg_probe_debug_view_mode_;

            p->debug_probe_radius = SsVg::dbg_probe_scale_ * 0.5f * cell_size_ / k_obm_per_voxel_resolution;
            p->debug_probe_near_geom_scale = SsVg::dbg_probe_near_geom_scale_;

            cbh_dispatch_->buffer_.Unmap();
        }

        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Main");

            if (is_first_dispatch)
            {
                // 初回クリア.
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InitClear");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_clear_voxel_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_clear_voxel_->SetView(&desc_set, "RWCoarseVoxelBuffer", coarse_voxel_data_.uav.Get());
                pso_clear_voxel_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());
                pso_clear_voxel_->SetView(&desc_set, "RWTexProbeSkyVisibility", probe_skyvisibility_.uav.Get());

                p_command_list->SetPipelineState(pso_clear_voxel_.Get());
                p_command_list->SetDescriptorSet(pso_clear_voxel_.Get(), &desc_set);
                pso_clear_voxel_->DispatchHelper(p_command_list, voxel_count, 1, 1);

                p_command_list->ResourceUavBarrier(coarse_voxel_data_.buffer.Get());
                p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
                p_command_list->ResourceUavBarrier(probe_skyvisibility_.texture.Get());
            }
            // Begin Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BeginUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_begin_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_begin_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_begin_update_->SetView(&desc_set, "RWCoarseVoxelBuffer", coarse_voxel_data_.uav.Get());
                pso_begin_update_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());
                pso_begin_update_->SetView(&desc_set, "RWVisibleCoarseVoxelList", visible_voxel_list_.uav.Get());

                p_command_list->SetPipelineState(pso_begin_update_.Get());
                p_command_list->SetDescriptorSet(pso_begin_update_.Get(), &desc_set);
                pso_begin_update_->DispatchHelper(p_command_list, voxel_count, 1, 1);

                p_command_list->ResourceUavBarrier(coarse_voxel_data_.buffer.Get());
                p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
                p_command_list->ResourceUavBarrier(visible_voxel_list_.buffer.Get());
            }
            // Voxelization Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ObmGeneration");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_voxelize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_voxelize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_voxelize_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_voxelize_->SetView(&desc_set, "RWOccupancyBitmaskVoxel", occupancy_bitmask_voxel_.uav.Get());
                pso_voxelize_->SetView(&desc_set, "RWVisibleCoarseVoxelList", visible_voxel_list_.uav.Get());

                p_command_list->SetPipelineState(pso_voxelize_.Get());
                p_command_list->SetDescriptorSet(pso_voxelize_.Get(), &desc_set);
                pso_voxelize_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

                p_command_list->ResourceUavBarrier(occupancy_bitmask_voxel_.buffer.Get());
                p_command_list->ResourceUavBarrier(visible_voxel_list_.buffer.Get());
            }
            // VisibleVoxel IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "GenerateVisibleElementIndirectArg");
                
                visible_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "VisibleCoarseVoxelList", visible_voxel_list_.srv.Get());
                pso_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "RWVisibleVoxelIndirectArg", visible_voxel_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_generate_visible_voxel_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_generate_visible_voxel_indirect_arg_.Get(), &desc_set);
                pso_generate_visible_voxel_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                visible_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Visible Voxel Update Pass.
            {
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VisibleUpdate");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_visible_probe_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_visible_probe_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_visible_probe_update_->SetView(&desc_set, "OccupancyBitmaskVoxel", occupancy_bitmask_voxel_.srv.Get());
                    pso_visible_probe_update_->SetView(&desc_set, "VisibleCoarseVoxelList", visible_voxel_list_.srv.Get());
                    pso_visible_probe_update_->SetView(&desc_set, "RWCoarseVoxelBuffer", coarse_voxel_data_.uav.Get());
                    pso_visible_probe_update_->SetView(&desc_set, "RWTexProbeSkyVisibility", probe_skyvisibility_.uav.Get());
                    pso_visible_probe_update_->SetView(&desc_set, "RWUpdateProbeWork", visible_voxel_update_probe_.uav.Get());

                    p_command_list->SetPipelineState(pso_visible_probe_update_.Get());
                    p_command_list->SetDescriptorSet(pso_visible_probe_update_.Get(), &desc_set);

                    p_command_list->DispatchIndirect(visible_voxel_indirect_arg_.buffer.Get());// こちらは可視VoxelのIndirect.


                    p_command_list->ResourceUavBarrier(visible_voxel_update_probe_.buffer.Get());
                    p_command_list->ResourceUavBarrier(coarse_voxel_data_.buffer.Get());
                }
                if(1)
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VisiblePostUpdate");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_visible_probe_post_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_visible_probe_post_update_->SetView(&desc_set, "VisibleCoarseVoxelList", visible_voxel_list_.srv.Get());
                    pso_visible_probe_post_update_->SetView(&desc_set, "UpdateProbeWork", visible_voxel_update_probe_.srv.Get());
                    pso_visible_probe_post_update_->SetView(&desc_set, "RWTexProbeSkyVisibility", probe_skyvisibility_.uav.Get());

                    p_command_list->SetPipelineState(pso_visible_probe_post_update_.Get());
                    p_command_list->SetDescriptorSet(pso_visible_probe_post_update_.Get(), &desc_set);

                    p_command_list->DispatchIndirect(visible_voxel_indirect_arg_.buffer.Get());// こちらは可視VoxelのIndirect.

                    p_command_list->ResourceUavBarrier(probe_skyvisibility_.texture.Get());
                }
            }
            // Coarse Voxel Update Pass.
            {
                if(1)
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "CoarseUpdate");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_coarse_probe_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_coarse_probe_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_coarse_probe_update_->SetView(&desc_set, "OccupancyBitmaskVoxel", occupancy_bitmask_voxel_.srv.Get());
                    pso_coarse_probe_update_->SetView(&desc_set, "VisibleCoarseVoxelList", visible_voxel_list_.srv.Get());
                    pso_coarse_probe_update_->SetView(&desc_set, "RWCoarseVoxelBuffer", coarse_voxel_data_.uav.Get());
                    pso_coarse_probe_update_->SetView(&desc_set, "RWTexProbeSkyVisibility", probe_skyvisibility_.uav.Get());

                    p_command_list->SetPipelineState(pso_coarse_probe_update_.Get());
                    p_command_list->SetDescriptorSet(pso_coarse_probe_update_.Get(), &desc_set);
                    // 全Probe更新のスキップ要素分考慮したDispatch.
                    pso_coarse_probe_update_->DispatchHelper(p_command_list, (voxel_count + (FRAME_UPDATE_ALL_PROBE_SKIP_COUNT - 1)) / FRAME_UPDATE_ALL_PROBE_SKIP_COUNT, 1, 1);


                    p_command_list->ResourceUavBarrier(coarse_voxel_data_.buffer.Get());
                }
            }
        }

        // デバッグ描画.
        if(SsVg::dbg_view_enable_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Debug");

            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_debug_visualize_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
            pso_debug_visualize_->SetView(&desc_set, "CoarseVoxelBuffer", coarse_voxel_data_.srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "OccupancyBitmaskVoxel", occupancy_bitmask_voxel_.srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "TexProbeSkyVisibility", probe_skyvisibility_.srv.Get());
            
            pso_debug_visualize_->SetView(&desc_set, "RWTexWork", work_uav.Get());

            p_command_list->SetPipelineState(pso_debug_visualize_.Get());
            p_command_list->SetDescriptorSet(pso_debug_visualize_.Get(), &desc_set);

            pso_debug_visualize_->DispatchHelper(p_command_list, work_tex_size.x, work_tex_size.y, 1);
        }
    }

    void SsVgCascade::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Debug");

        
        // Viewport.
        gfx::helper::SetFullscreenViewportAndScissor(p_command_list, lighting_tex->GetWidth(), lighting_tex->GetHeight());

        // Rtv, Dsv セット.
        {
            const auto* p_rtv = lighting_rtv.Get();
            p_command_list->SetRenderTargets(&p_rtv, 1, hw_depth_dsv.Get());
        }

        if (0 <= SsVg::dbg_probe_debug_view_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ProbeDebug");

            const int coarse_voxel_count = base_resolution_.x * base_resolution_.y * base_resolution_.z;

            p_command_list->SetPipelineState(pso_debug_obm_voxel_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_debug_obm_voxel_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_debug_obm_voxel_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());
            
            pso_debug_obm_voxel_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
            pso_debug_obm_voxel_->SetView(&desc_set, "CoarseVoxelBuffer", coarse_voxel_data_.srv.Get());
            pso_debug_obm_voxel_->SetView(&desc_set, "OccupancyBitmaskVoxel", occupancy_bitmask_voxel_.srv.Get());
            pso_debug_obm_voxel_->SetView(&desc_set, "TexProbeSkyVisibility", probe_skyvisibility_.srv.Get());
            pso_debug_obm_voxel_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_debug_obm_voxel_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * coarse_voxel_count, 1, 0, 0);
        }

    }


    // ----------------------------------------------------------------

    
    SsVg::~SsVg()
    {
        for(auto& c : cascades_)
        {
            if(c)
            {
                delete c;
                c = nullptr;
            }
        }
        cascades_.clear();
    }

    // 初期化
    bool SsVg::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u base_resolution, float cell_size, int cascade_count)
    {
        for (int i = 0; i < cascade_count; ++i)
        {
            SsVgCascade* c = new SsVgCascade();
            if(!c->Initialize(p_device, base_resolution, cell_size * (1 << i)))
            {
                delete c;
                return false;
            }
            cascades_.push_back(c);
        }
        return true;
    }

    void SsVg::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        for(auto& c : cascades_)
        {
            c->Dispatch(p_command_list, scene_cbv, hw_depth_tex, hw_depth_srv, work_tex, work_uav);
        }
    }

    void SsVg::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        for(auto& c : cascades_)
        {
            c->DebugDraw(p_command_list, scene_cbv, hw_depth_tex, hw_depth_dsv, lighting_tex, lighting_rtv);
        }
    }

    void SsVg::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        for(auto& c : cascades_)
        {
            c->SetImportantPointInfo(pos, dir);
        }
    }


}  // namespace ngl::render::app
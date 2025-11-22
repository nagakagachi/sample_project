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
    
    #define NGL_SHADER_CPP_INCLUDE
    // cpp/hlsl共通定義用ヘッダ.
    #include "../shader/ssvg/ssvg_common_header.hlsli"
    #undef NGL_SHADER_CPP_INCLUDE


    static constexpr size_t k_sizeof_BbvOptionalData = sizeof(BbvOptionalData);
    static constexpr size_t k_sizeof_WcpProbeData      = sizeof(WcpProbeData);
    static constexpr u32 k_max_update_probe_work_count = 1024;

    // デバッグ.
    int SsVg::dbg_view_mode_ = -1;
    int SsVg::dbg_bbv_probe_debug_mode_ = -1;
    int SsVg::dbg_wcp_probe_debug_mode_ = -1;
    float SsVg::dbg_probe_scale_ = 1.0f;
    float SsVg::dbg_probe_near_geom_scale_ = 0.2f;
    

    void ToroidalGridUpdater::Initialize(const math::Vec3u& grid_resolution, float bbv_cell_size)
    {
        grid_.resolution_ = grid_resolution;
        grid_.cell_size_ = bbv_cell_size;

        const u32 total_count = grid_.resolution_.x * grid_.resolution_.y * grid_.resolution_.z;
        grid_.total_count = total_count;
        grid_.flatten_2d_width_ = static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(total_count))));
    }
    void ToroidalGridUpdater::UpdateGrid(const math::Vec3& important_pos)
    {
        // 中心を離散CELLIDで保持.
        grid_.center_cell_id_prev_ = grid_.center_cell_id_;
        grid_.center_cell_id_      = (important_pos / grid_.cell_size_).Cast<int>();

        // 離散CELLIDからGridMin情報を復元.
        grid_.min_pos_prev_ = grid_.center_cell_id_prev_.Cast<float>() * grid_.cell_size_ - grid_.resolution_.Cast<float>() * 0.5f * grid_.cell_size_;
        grid_.min_pos_      = grid_.center_cell_id_.Cast<float>() * grid_.cell_size_ - grid_.resolution_.Cast<float>() * 0.5f * grid_.cell_size_;

        grid_.min_pos_delta_cell_ = grid_.center_cell_id_ - grid_.center_cell_id_prev_;

        grid_.toroidal_offset_prev_ = grid_.toroidal_offset_;
        // シフトコピーをせずにToroidalにアクセスするためのオフセット. このオフセットをした後に mod を取った位置にアクセスする. その外側はInvalidateされる.
        grid_.toroidal_offset_ = (((grid_.toroidal_offset_ +  grid_.min_pos_delta_cell_) % grid_.resolution_.Cast<int>()) + grid_.resolution_.Cast<int>()) % grid_.resolution_.Cast<int>();
    }
    math::Vec3i ToroidalGridUpdater::CalcToroidalGridCoordFromLinearCoord(const math::Vec3i& linear_coord) const
    {
        return (linear_coord + grid_.toroidal_offset_) % grid_.resolution_.Cast<int>();
    }
    math::Vec3i ToroidalGridUpdater::CalcLinearGridCoordFromToroidalCoord(const math::Vec3i& toroidal_coord) const
    {
        return (toroidal_coord + (grid_.resolution_.Cast<int>() - grid_.toroidal_offset_)) % grid_.resolution_.Cast<int>();
    }


    BitmaskBrickVoxel::~BitmaskBrickVoxel()
    {
    }

    // 初期化
    bool BitmaskBrickVoxel::Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg)
    {
        bbv_grid_updater_.Initialize(init_arg.voxel_resolution, init_arg.voxel_size);
        wcp_grid_updater_.Initialize(init_arg.probe_resolution, init_arg.probe_cell_size);


        const u32 voxel_count = bbv_grid_updater_.Get().resolution_.x * bbv_grid_updater_.Get().resolution_.y * bbv_grid_updater_.Get().resolution_.z;
        // サーフェイスVoxelのリスト. スクリーン上でサーフェイスとして充填された要素を詰め込む. Bbvの充填とは別で, 後処理でサーフェイスVoxelを処理するためのリスト.
        bbv_fine_update_voxel_count_max_= std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);

        // 中空Voxelのクリアキューサイズ. スクリーン上で中空判定された要素を詰め込む.
        bbv_hollow_voxel_list_count_max_= 1024*2;//std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);


        const u32 wcp_probe_cell_count = wcp_grid_updater_.Get().resolution_.x * wcp_grid_updater_.Get().resolution_.y * wcp_grid_updater_.Get().resolution_.z;
        wcp_visible_surface_buffer_size_ = k_max_update_probe_work_count;

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
            pso_bbv_clear_  = CreateComputePSO("ssvg/bbv_clear_voxel_cs.hlsl");
            pso_bbv_begin_update_ = CreateComputePSO("ssvg/bbv_begin_update_cs.hlsl");
            pso_bbv_hollow_voxel_info_ = CreateComputePSO("ssvg/bbv_generate_hollow_voxel_info_cs.hlsl");
            pso_bbv_remove_hollow_voxel_ = CreateComputePSO("ssvg/bbv_remove_hollow_voxel_cs.hlsl");
            pso_bbv_voxelize_     = CreateComputePSO("ssvg/bbv_generate_visible_surface_voxel_cs.hlsl");
            pso_bbv_generate_visible_voxel_indirect_arg_ = CreateComputePSO("ssvg/bbv_generate_visible_surface_list_indirect_arg_cs.hlsl");
            pso_bbv_element_update_ = CreateComputePSO("ssvg/bbv_element_update_cs.hlsl");
            pso_bbv_visible_surface_element_update_ = CreateComputePSO("ssvg/bbv_visible_surface_element_update_cs.hlsl");

            pso_wcp_clear_ = CreateComputePSO("ssvg/wcp_clear_voxel_cs.hlsl");
            pso_wcp_begin_update_ = CreateComputePSO("ssvg/wcp_begin_update_cs.hlsl");
            pso_wcp_visible_surface_proc_ = CreateComputePSO("ssvg/wcp_screen_space_pass_cs.hlsl");
            pso_wcp_generate_visible_surface_list_indirect_arg_ = CreateComputePSO("ssvg/wcp_generate_visible_surface_list_indirect_arg_cs.hlsl");
            pso_wcp_visible_surface_element_update_ = CreateComputePSO("ssvg/wcp_visible_surface_element_update_cs.hlsl");
            pso_wcp_coarse_ray_sample_ = CreateComputePSO("ssvg/wcp_element_update_cs.hlsl");
            pso_wcp_fill_probe_octmap_atlas_border_ = CreateComputePSO("ssvg/wcp_fill_probe_octmap_atlas_border_cs.hlsl");

            
            // デバッグ用PSO.
            {
                pso_bbv_debug_visualize_ = CreateComputePSO("ssvg/debug_util/voxel_debug_visualize_cs.hlsl");
                
                {
                    pso_bbv_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                    ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                    {
                        ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                        vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                        vs_load_desc.shader_model_version          = k_shader_model;
                        vs_load_desc.entry_point_name              = "main_vs";
                        auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/voxel_probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/voxel_probe_debug_ps.hlsl"), &ps_load_desc);
                        gpso_desc.ps = &ps_load_handle->data_;
                    }

                    gpso_desc.num_render_targets = 1;
                    gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                    gpso_desc.depth_stencil_state.depth_enable = true;
                    gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                    gpso_desc.depth_stencil_state.depth_write_enable = true;
                    gpso_desc.depth_stencil_state.stencil_enable = false;
                    gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;
                    
                    if(!pso_bbv_debug_probe_->Initialize(p_device, gpso_desc))
                    {
                        return false;
                    }
                }
                {
                    pso_wcp_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                    ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                    {
                        ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                        vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                        vs_load_desc.shader_model_version          = k_shader_model;
                        vs_load_desc.entry_point_name              = "main_vs";
                        auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("ssvg/debug_util/probe_debug_ps.hlsl"), &ps_load_desc);
                        gpso_desc.ps = &ps_load_handle->data_;
                    }

                    gpso_desc.num_render_targets = 1;
                    gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                    gpso_desc.depth_stencil_state.depth_enable = true;
                    gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                    gpso_desc.depth_stencil_state.depth_write_enable = true;
                    gpso_desc.depth_stencil_state.stencil_enable = false;
                    gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;

                    if(!pso_wcp_debug_probe_->Initialize(p_device, gpso_desc))
                    {
                        return false;
                    }
                }
            }
        }


        {
            bbv_optional_data_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(BbvOptionalData),
                                               .element_count     = voxel_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default});
        }
        {
            bbv_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = voxel_count * k_bbv_per_voxel_u32_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            bbv_fine_update_voxel_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = bbv_fine_update_voxel_count_max_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            bbv_fine_update_voxel_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            // 1F更新可能プローブ数分の k_probe_octmap_width*k_probe_octmap_width テクセル分バッファ.
            bbv_fine_update_voxel_probe_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(float),
                                               .element_count     = bbv_fine_update_voxel_count_max_ * (k_probe_octmap_width*k_probe_octmap_width),

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_FLOAT);
        }
        
        {
            bbv_remove_voxel_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = (bbv_hollow_voxel_list_count_max_+1) * k_component_count_RemoveVoxelList,// 0番目にアトミックカウンタ用途.　格納情報にuint2相当が必要且つAtomic操作のために2倍サイズのScalarバッファとしている.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }

        {
            // wcp_buffer_初期化.
            wcp_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(WcpProbeData),
                                               .element_count     = wcp_probe_cell_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default});
        }
        {
            wcp_visible_surface_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = wcp_visible_surface_buffer_size_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }
        {
            wcp_visible_surface_list_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }

        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  wcp_grid_updater_.Get().flatten_2d_width_ * k_probe_octmap_width_with_border;
            desc.height = static_cast<u32>(std::ceil((wcp_grid_updater_.Get().total_count + wcp_grid_updater_.Get().flatten_2d_width_-1) / wcp_grid_updater_.Get().flatten_2d_width_)) * k_probe_octmap_width_with_border;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16_FLOAT;
            //desc.format = rhi::EResourceFormat::Format_R8_UNORM;
            //desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::UnorderedAccess;

            wcp_probe_atlas_tex_.Initialize(p_device, desc);
        }

        return true;
    }

    void BitmaskBrickVoxel::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        important_point_ = pos;
        important_dir_   = dir;
    }

    void BitmaskBrickVoxel::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
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
            bbv_grid_updater_.UpdateGrid(modified_important_point);
            wcp_grid_updater_.UpdateGrid(modified_important_point);
        }

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));

        cbh_dispatch_ = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SsvgParam));
        {
            auto* p = cbh_dispatch_->buffer_.MapAs<SsvgParam>();

            //Bbv
            {
                p->bbv.grid_resolution = bbv_grid_updater_.Get().resolution_.Cast<int>();
                p->bbv.grid_min_pos     = bbv_grid_updater_.Get().min_pos_;
                p->bbv.grid_min_voxel_coord = math::Vec3::Floor(bbv_grid_updater_.Get().min_pos_ * (1.0f / bbv_grid_updater_.Get().cell_size_)).Cast<int>();

                p->bbv.grid_toroidal_offset =  bbv_grid_updater_.Get().toroidal_offset_;
                p->bbv.grid_toroidal_offset_prev =  bbv_grid_updater_.Get().toroidal_offset_prev_;

                p->bbv.grid_move_cell_delta = bbv_grid_updater_.Get().min_pos_delta_cell_;

                p->bbv.flatten_2d_width = bbv_grid_updater_.Get().flatten_2d_width_;

                p->bbv.cell_size       = bbv_grid_updater_.Get().cell_size_;
                p->bbv.cell_size_inv    = 1.0f / bbv_grid_updater_.Get().cell_size_;

                p->bbv_indirect_cs_thread_group_size = math::Vec3i(pso_bbv_visible_surface_element_update_->GetThreadGroupSizeX(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeY(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeZ());
                p->bbv_visible_voxel_buffer_size = bbv_fine_update_voxel_count_max_;
                p->bbv_hollow_voxel_buffer_size = bbv_hollow_voxel_list_count_max_;
            }
            // Wcp
            {
                p->wcp.grid_resolution = wcp_grid_updater_.Get().resolution_.Cast<int>();
                p->wcp.grid_min_pos     = wcp_grid_updater_.Get().min_pos_;
                p->wcp.grid_min_voxel_coord = math::Vec3::Floor(wcp_grid_updater_.Get().min_pos_ * (1.0f / wcp_grid_updater_.Get().cell_size_)).Cast<int>();

                p->wcp.grid_toroidal_offset =  wcp_grid_updater_.Get().toroidal_offset_;
                p->wcp.grid_toroidal_offset_prev =  wcp_grid_updater_.Get().toroidal_offset_prev_;

                p->wcp.grid_move_cell_delta = wcp_grid_updater_.Get().min_pos_delta_cell_;

                p->wcp.flatten_2d_width = wcp_grid_updater_.Get().flatten_2d_width_;

                p->wcp.cell_size       = wcp_grid_updater_.Get().cell_size_;
                p->wcp.cell_size_inv    = 1.0f / wcp_grid_updater_.Get().cell_size_;

                p->wcp_indirect_cs_thread_group_size = math::Vec3i(pso_wcp_visible_surface_element_update_->GetThreadGroupSizeX(), pso_wcp_visible_surface_element_update_->GetThreadGroupSizeY(), pso_wcp_visible_surface_element_update_->GetThreadGroupSizeZ());
                p->wcp_visible_voxel_buffer_size = wcp_visible_surface_buffer_size_;
            }


            p->tex_hw_depth_size = hw_depth_size;
            p->frame_count = frame_count_;

            p->debug_view_mode = SsVg::dbg_view_mode_;
            p->debug_bbv_probe_mode = SsVg::dbg_bbv_probe_debug_mode_;
            p->debug_wcp_probe_mode = SsVg::dbg_wcp_probe_debug_mode_;

            p->debug_probe_radius = SsVg::dbg_probe_scale_ * 0.5f * bbv_grid_updater_.Get().cell_size_ / k_bbv_per_voxel_resolution;
            p->debug_probe_near_geom_scale = SsVg::dbg_probe_near_geom_scale_;

            cbh_dispatch_->buffer_.Unmap();
        }

        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Main");

            if (is_first_dispatch)
            {
                // 初回クリア.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvInitClear");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_bbv_clear_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_bbv_clear_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());
                    pso_bbv_clear_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

                    p_command_list->SetPipelineState(pso_bbv_clear_.Get());
                    p_command_list->SetDescriptorSet(pso_bbv_clear_.Get(), &desc_set);
                    pso_bbv_clear_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

                    p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
                }
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpInitClear");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_clear_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_clear_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                    pso_wcp_clear_->SetView(&desc_set, "RWWcpProbeAtlasTex", wcp_probe_atlas_tex_.uav.Get());

                    p_command_list->SetPipelineState(pso_wcp_clear_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_clear_.Get(), &desc_set);
                    pso_wcp_clear_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                    p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                }
            }
            // Bbv Begin Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_begin_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_bbv_begin_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());
                pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
                pso_bbv_begin_update_->SetView(&desc_set, "RWVisibleVoxelList", bbv_fine_update_voxel_list_.uav.Get());
                pso_bbv_begin_update_->SetView(&desc_set, "RWRemoveVoxelList", bbv_remove_voxel_list_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_begin_update_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_begin_update_.Get(), &desc_set);
                pso_bbv_begin_update_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

                p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_fine_update_voxel_list_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_remove_voxel_list_.buffer.Get());
            }
                // Wcp Begin Update Pass.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpBeginUpdate");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_begin_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_wcp_begin_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_begin_update_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                    pso_wcp_begin_update_->SetView(&desc_set, "RWWcpProbeAtlasTex", wcp_probe_atlas_tex_.uav.Get());
                    pso_wcp_begin_update_->SetView(&desc_set, "RWSurfaceProbeCellList", wcp_visible_surface_list_.uav.Get());

                    p_command_list->SetPipelineState(pso_wcp_begin_update_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_begin_update_.Get(), &desc_set);
                    pso_wcp_begin_update_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                    p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                    p_command_list->ResourceUavBarrier(wcp_visible_surface_list_.buffer.Get());
                }

                
            // Bbv Generate Remove Voxel Info.
            // 動的な環境で中空になった可能性のあるBbvをクリアするためのリスト生成. Depthからその表面に至るまでの経路上のVoxelが中空であると仮定してリスト化.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "GenerageHolowVoxelInfo");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_hollow_voxel_info_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_bbv_hollow_voxel_info_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_bbv_hollow_voxel_info_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_hollow_voxel_info_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_bbv_hollow_voxel_info_->SetView(&desc_set, "RWRemoveVoxelList", bbv_remove_voxel_list_.uav.Get());
                p_command_list->SetPipelineState(pso_bbv_hollow_voxel_info_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_hollow_voxel_info_.Get(), &desc_set);
                pso_bbv_hollow_voxel_info_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

                p_command_list->ResourceUavBarrier(bbv_remove_voxel_list_.buffer.Get());
            }
            // Bbv Remove Voxel Pass.
            // Note:前段で生成した中空Voxelを実際にBbvバッファ上で空にする. なおBbv単位が持つ非Emptyフラグの更新をしていないため後で問題になるかも.
            if(true)
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "RemoveHollowVoxel");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_remove_hollow_voxel_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_remove_hollow_voxel_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
                pso_bbv_remove_hollow_voxel_->SetView(&desc_set, "RemoveVoxelList", bbv_remove_voxel_list_.srv.Get());
                p_command_list->SetPipelineState(pso_bbv_remove_hollow_voxel_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_remove_hollow_voxel_.Get(), &desc_set);
                pso_bbv_remove_hollow_voxel_->DispatchHelper(p_command_list, bbv_hollow_voxel_list_count_max_, 1, 1);// 現状は最大数分Dispatch. あとでIndirectArg化.

                p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
            }

            // Bbv Voxelization Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BitmaskBrickVoxelGeneration");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_voxelize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_bbv_voxelize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_bbv_voxelize_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_voxelize_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
                pso_bbv_voxelize_->SetView(&desc_set, "RWVisibleVoxelList", bbv_fine_update_voxel_list_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_voxelize_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_voxelize_.Get(), &desc_set);
                pso_bbv_voxelize_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

                p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_fine_update_voxel_list_.buffer.Get());
            }
            // VisibleVoxel IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "GenerateVisibleElementIndirectArg");
                
                bbv_fine_update_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "VisibleVoxelList", bbv_fine_update_voxel_list_.srv.Get());
                pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "RWVisibleVoxelIndirectArg", bbv_fine_update_voxel_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_generate_visible_voxel_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_generate_visible_voxel_indirect_arg_.Get(), &desc_set);
                pso_bbv_generate_visible_voxel_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                bbv_fine_update_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Voxel Update.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ProbeCommonUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_element_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                pso_bbv_element_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                pso_bbv_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_bbv_element_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_element_update_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_element_update_.Get(), &desc_set);
                //pso_bbv_element_update_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);
                pso_bbv_element_update_->DispatchHelper(p_command_list, (bbv_grid_updater_.Get().total_count + (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT)) / (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT+1), 1, 1);

                p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
            }

            // Visible Surface Voxel Update Pass.
            {
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VisibleSurfaceVoxelUpdate");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "VisibleVoxelList", bbv_fine_update_voxel_list_.srv.Get());

                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());

                    pso_bbv_visible_surface_element_update_->SetView(&desc_set, "RWUpdateProbeWork", bbv_fine_update_voxel_probe_buffer_.uav.Get());

                    p_command_list->SetPipelineState(pso_bbv_visible_surface_element_update_.Get());
                    p_command_list->SetDescriptorSet(pso_bbv_visible_surface_element_update_.Get(), &desc_set);

                    p_command_list->DispatchIndirect(bbv_fine_update_voxel_indirect_arg_.buffer.Get());// こちらは可視VoxelのIndirect.


                    p_command_list->ResourceUavBarrier(bbv_fine_update_voxel_probe_buffer_.buffer.Get());
                }
            }
            
            
                // Wcp Visible Surface Processing Pass.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpVisibleSurfaceProcessing");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_visible_surface_proc_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                    pso_wcp_visible_surface_proc_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_wcp_visible_surface_proc_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_visible_surface_proc_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                    pso_wcp_visible_surface_proc_->SetView(&desc_set, "RWSurfaceProbeCellList", wcp_visible_surface_list_.uav.Get());

                    p_command_list->SetPipelineState(pso_wcp_visible_surface_proc_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_visible_surface_proc_.Get(), &desc_set);
                    pso_wcp_visible_surface_proc_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

                    p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(wcp_visible_surface_list_.buffer.Get());
                }
                // Wcp VisibleSurfaceList IndirectArg生成.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpGenerateVisibleSurfaceListIndirectArg");
                    
                    wcp_visible_surface_list_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_generate_visible_surface_list_indirect_arg_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_generate_visible_surface_list_indirect_arg_->SetView(&desc_set, "SurfaceProbeCellList", wcp_visible_surface_list_.srv.Get());
                    pso_wcp_generate_visible_surface_list_indirect_arg_->SetView(&desc_set, "RWVisibleSurfaceListIndirectArg", wcp_visible_surface_list_indirect_arg_.uav.Get());

                    p_command_list->SetPipelineState(pso_wcp_generate_visible_surface_list_indirect_arg_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_generate_visible_surface_list_indirect_arg_.Get(), &desc_set);
                    pso_wcp_generate_visible_surface_list_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                    wcp_visible_surface_list_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
                }
                // Wcp Visible Surface Element RaySample Pass.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpVisibleSurfaceElementRaySample");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "SurfaceProbeCellList", wcp_visible_surface_list_.srv.Get());
                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                    pso_wcp_visible_surface_element_update_->SetView(&desc_set, "RWWcpProbeAtlasTex", wcp_probe_atlas_tex_.uav.Get());


                    p_command_list->SetPipelineState(pso_wcp_visible_surface_element_update_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_visible_surface_element_update_.Get(), &desc_set);

                    p_command_list->DispatchIndirect(wcp_visible_surface_list_indirect_arg_.buffer.Get());// 可視SurfaceListDispatch.


                    p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                }
                // Wcp Coarse Probe RaySample Pass.
                {
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpCoarseProbeRaySample");

                        ngl::rhi::DescriptorSetDep desc_set = {};
                        pso_wcp_coarse_ray_sample_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
                        pso_wcp_coarse_ray_sample_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                        pso_wcp_coarse_ray_sample_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                        pso_wcp_coarse_ray_sample_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                        pso_wcp_coarse_ray_sample_->SetView(&desc_set, "RWWcpProbeAtlasTex", wcp_probe_atlas_tex_.uav.Get());

                        p_command_list->SetPipelineState(pso_wcp_coarse_ray_sample_.Get());
                        p_command_list->SetDescriptorSet(pso_wcp_coarse_ray_sample_.Get(), &desc_set);
                        // 全Probe更新のスキップ要素分考慮したDispatch.
                        pso_wcp_coarse_ray_sample_->DispatchHelper(p_command_list, (wcp_grid_updater_.Get().total_count + (WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT)) / (WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT+1), 1, 1);

                        p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                        p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                    }
                }
                // Wcp Octahedral Map Border Fill Pass.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpFillProbeOctmapAtlasBorder");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_wcp_fill_probe_octmap_atlas_border_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
                    pso_wcp_fill_probe_octmap_atlas_border_->SetView(&desc_set, "RWWcpProbeAtlasTex", wcp_probe_atlas_tex_.uav.Get());

                    p_command_list->SetPipelineState(pso_wcp_fill_probe_octmap_atlas_border_.Get());
                    p_command_list->SetDescriptorSet(pso_wcp_fill_probe_octmap_atlas_border_.Get(), &desc_set);

                    // 全Probe更新のスキップ要素分考慮したDispatch.
                    pso_wcp_fill_probe_octmap_atlas_border_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                    p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                }
        }

        // デバッグ描画.
        if(0 <= SsVg::dbg_view_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Debug");

            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "WcpProbeAtlasTex", wcp_probe_atlas_tex_.srv.Get());
            
            pso_bbv_debug_visualize_->SetView(&desc_set, "RWTexWork", work_uav.Get());

            p_command_list->SetPipelineState(pso_bbv_debug_visualize_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_debug_visualize_.Get(), &desc_set);

            pso_bbv_debug_visualize_->DispatchHelper(p_command_list, work_tex_size.x, work_tex_size.y, 1);
        }
    }

    void BitmaskBrickVoxel::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
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

        if (0 <= SsVg::dbg_bbv_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvProbeDebug");

            p_command_list->SetPipelineState(pso_bbv_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_bbv_debug_probe_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);
            
            pso_bbv_debug_probe_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_probe_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_bbv_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * bbv_grid_updater_.Get().total_count, 1, 0, 0);
        }
        if (0 <= SsVg::dbg_wcp_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpProbeDebug");

            p_command_list->SetPipelineState(pso_wcp_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_wcp_debug_probe_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);

            pso_wcp_debug_probe_->SetView(&desc_set, "cb_ssvg", &cbh_dispatch_->cbv_);
            pso_wcp_debug_probe_->SetView(&desc_set, "WcpProbeBuffer", wcp_buffer_.srv.Get());
            pso_wcp_debug_probe_->SetView(&desc_set, "WcpProbeAtlasTex", wcp_probe_atlas_tex_.srv.Get());
            pso_wcp_debug_probe_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_wcp_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * wcp_grid_updater_.Get().total_count, 1, 0, 0);
        }

    }


    // ----------------------------------------------------------------

    
    SsVg::~SsVg()
    {
        Finalize();
    }

    // 初期化
    bool SsVg::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u wcp_resolution, float wcp_cell_size)
    {
        ssvg_instance_ = new BitmaskBrickVoxel();
        BitmaskBrickVoxel::InitArg init_arg = {};
        {
            init_arg.voxel_resolution = bbv_resolution;
            init_arg.voxel_size       = bbv_cell_size;

            init_arg.probe_resolution = wcp_resolution;
            init_arg.probe_cell_size  = wcp_cell_size;
        }
        if(!ssvg_instance_->Initialize(p_device, init_arg))
        {
            delete ssvg_instance_;
            ssvg_instance_ = nullptr;
            return false;
        }

        is_initialized_ = true;
        return true;
    }
    // 破棄
    void SsVg::Finalize()
    {
        if(ssvg_instance_)
        {
            delete ssvg_instance_;
            ssvg_instance_ = nullptr;
        }
        is_initialized_ = false;
    }

    void SsVg::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        if(ssvg_instance_)
        {
            ssvg_instance_->Dispatch(p_command_list, scene_cbv, hw_depth_tex, hw_depth_srv, work_tex, work_uav);
        }
    }

    void SsVg::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        if(ssvg_instance_)
        {
            ssvg_instance_->DebugDraw(p_command_list, scene_cbv, hw_depth_tex, hw_depth_dsv, lighting_tex, lighting_rtv);
        }
    }

    void SsVg::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        if(ssvg_instance_)
        {
            ssvg_instance_->SetImportantPointInfo(pos, dir);
        }
    }

    void SsVg::SetDescriptorCascade0(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const
    {
        assert(ssvg_instance_);
        p_pso->SetView(p_desc_set, "WcpProbeAtlasTex", ssvg_instance_->GetWcpProbeAtlasTex().Get());
        p_pso->SetView(p_desc_set, "cb_ssvg", &ssvg_instance_->GetDispatchCbh()->cbv_);
    }

}  // namespace ngl::render::app
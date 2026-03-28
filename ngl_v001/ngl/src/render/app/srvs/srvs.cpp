/*
    srvs.cpp
    screen-reconstructed voxel structure.
*/

#include "render/app/srvs/srvs.h"

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
    #include "../shader/srvs/srvs_common_header.hlsli"
    #undef NGL_SHADER_CPP_INCLUDE


    static constexpr size_t k_sizeof_BbvOptionalData = sizeof(BbvOptionalData);
    static constexpr size_t k_sizeof_WcpProbeData      = sizeof(WcpProbeData);
    static constexpr u32 k_max_update_probe_work_count = 1024;
    
    // 時間分散するScreenProbeグループのサイズ. 幅がこのサイズのProbeグループ毎に1Fに一つ更新をする. GI-1.0などは2を指定して 4フレームで2x2のグループが更新される.
    static const int k_ss_probe_update_skip_tile_group_width = 1;
    static const float k_ss_probe_ray_start_offset_scale = sqrt(3.0f);
    static const float k_ss_probe_ray_normal_offset_scale = 0.2f;
    static const float k_ss_probe_temporal_depth_threshold = 0.02f;
    
    static const float k_ss_probe_temporal_min_hysteresis = 0.85f;
    static const float k_ss_probe_temporal_max_hysteresis = 0.98f;
    static const int k_ss_probe_side_cache_max_life_frame = 24;


    // デバッグ.
    int ScreenReconstructedVoxelStructure::dbg_view_mode_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_bbv_probe_debug_mode_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_wcp_probe_debug_mode_ = -1;
    float ScreenReconstructedVoxelStructure::dbg_probe_scale_ = 1.0f;
    float ScreenReconstructedVoxelStructure::dbg_probe_near_geom_scale_ = 0.2f;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_reprojection_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_ray_guiding_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_enable_ = 1;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_preupdate_relocation_probability_ = static_cast<float>(SCREEN_SPACE_PROBE_PREUPDATE_RELOCATION_PROBABILITY);
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_normal_cos_threshold_ = static_cast<float>(SCREEN_SPACE_PROBE_TEMPORAL_FILTER_NORMAL_COS_THRESHOLD);
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_plane_dist_threshold_ = static_cast<float>(SCREEN_SPACE_PROBE_TEMPORAL_FILTER_PLANE_DIST_THRESHOLD);
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_normal_cos_threshold_ = static_cast<float>(SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD);
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_depth_exp_scale_ = static_cast<float>(SCREEN_SPACE_PROBE_SPATIAL_FILTER_DEPTH_EXP_SCALE);
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_plane_dist_threshold_ = static_cast<float>(SCREEN_SPACE_PROBE_SIDE_CACHE_PLANE_THRESHOLD);
    
    using SrvsShaderBindName = ngl::text::HashText<128>;
    constexpr SrvsShaderBindName k_shader_bind_name_wcp_atlas_srv = "WcpProbeAtlasTex";
    constexpr SrvsShaderBindName k_shader_bind_name_wcp_atlas_uav = "RWWcpProbeAtlasTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_srv = "ScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_history_srv = "ScreenSpaceProbeHistoryTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_uav = "RWScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_tile_info_srv = "ScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_history_tile_info_srv = "ScreenSpaceProbeHistoryTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_tile_info_uav = "RWScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_filtered_uav = "RWScreenSpaceProbeFilteredTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_sh_srv = "ScreenSpaceProbeSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_sh_uav = "RWScreenSpaceProbeSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_srv = "ScreenSpaceProbeSideCacheTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_uav = "RWScreenSpaceProbeSideCacheTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_meta_srv = "ScreenSpaceProbeSideCacheMetaTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_meta_uav = "RWScreenSpaceProbeSideCacheMetaTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_lock_uav = "RWScreenSpaceProbeSideCacheLockTex";

    void ToroidalGridUpdater::Initialize(const math::Vec3u& grid_resolution, float bbv_cell_size)
    {
        grid_.resolution = grid_resolution;
        grid_.cell_size = bbv_cell_size;

        const u32 total_count = grid_.resolution.x * grid_.resolution.y * grid_.resolution.z;
        grid_.total_count = total_count;
        grid_.flatten_2d_width = static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(total_count))));
    }
    void ToroidalGridUpdater::UpdateGrid(const math::Vec3& important_pos)
    {
        // 中心を離散CELLIDで保持.
        grid_.center_cell_id_prev = grid_.center_cell_id;
        grid_.center_cell_id      = (important_pos / grid_.cell_size).Cast<int>();

        // 離散CELLIDからGridMin情報を復元.
        grid_.min_pos_prev = grid_.center_cell_id_prev.Cast<float>() * grid_.cell_size - grid_.resolution.Cast<float>() * 0.5f * grid_.cell_size;
        grid_.min_pos      = grid_.center_cell_id.Cast<float>() * grid_.cell_size - grid_.resolution.Cast<float>() * 0.5f * grid_.cell_size;

        grid_.min_pos_delta_cell = grid_.center_cell_id - grid_.center_cell_id_prev;

        grid_.toroidal_offset_prev = grid_.toroidal_offset;
        // シフトコピーをせずにToroidalにアクセスするためのオフセット. このオフセットをした後に mod を取った位置にアクセスする. その外側はInvalidateされる.
        grid_.toroidal_offset = (((grid_.toroidal_offset +  grid_.min_pos_delta_cell) % grid_.resolution.Cast<int>()) + grid_.resolution.Cast<int>()) % grid_.resolution.Cast<int>();
    }
    math::Vec3i ToroidalGridUpdater::CalcToroidalGridCoordFromLinearCoord(const math::Vec3i& linear_coord) const
    {
        return (linear_coord + grid_.toroidal_offset) % grid_.resolution.Cast<int>();
    }
    math::Vec3i ToroidalGridUpdater::CalcLinearGridCoordFromToroidalCoord(const math::Vec3i& toroidal_coord) const
    {
        return (toroidal_coord + (grid_.resolution.Cast<int>() - grid_.toroidal_offset)) % grid_.resolution.Cast<int>();
    }


    BitmaskBrickVoxelGi::~BitmaskBrickVoxelGi()
    {
    }

    // 初期化
    bool BitmaskBrickVoxelGi::Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg)
    {
        bbv_grid_updater_.Initialize(init_arg.voxel_resolution, init_arg.voxel_size);
        wcp_grid_updater_.Initialize(init_arg.probe_resolution, init_arg.probe_cell_size);


        const u32 voxel_count = bbv_grid_updater_.Get().resolution.x * bbv_grid_updater_.Get().resolution.y * bbv_grid_updater_.Get().resolution.z;
        // サーフェイスVoxelのリスト. スクリーン上でサーフェイスとして充填された要素を詰め込む. Bbvの充填とは別で, 後処理でサーフェイスVoxelを処理するためのリスト.
        bbv_fine_update_voxel_count_max_= std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);

        // 中空Voxelのクリアキューサイズ. スクリーン上で中空判定された要素を詰め込む.
        bbv_hollow_voxel_list_count_max_= 1024*2;//std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);


        const u32 wcp_probe_cell_count = wcp_grid_updater_.Get().resolution.x * wcp_grid_updater_.Get().resolution.y * wcp_grid_updater_.Get().resolution.z;
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
            auto* pso_cache = p_device->GetPipelineStateCache();
            return pso_cache->GetOrCreate(p_device, cpso_desc);
        };
        {
            pso_bbv_clear_  = CreateComputePSO("srvs/bbv_clear_voxel_cs.hlsl");
            pso_bbv_begin_update_ = CreateComputePSO("srvs/bbv_begin_update_cs.hlsl");
            pso_bbv_begin_view_update_ = CreateComputePSO("srvs/bbv_begin_view_update_cs.hlsl");
            pso_bbv_removal_list_build_ = CreateComputePSO("srvs/bbv_removal_list_build_cs.hlsl");
            pso_bbv_removal_apply_ = CreateComputePSO("srvs/bbv_removal_apply_cs.hlsl");
            pso_bbv_injection_apply_     = CreateComputePSO("srvs/bbv_injection_apply_cs.hlsl");
            pso_bbv_generate_visible_voxel_indirect_arg_ = CreateComputePSO("srvs/bbv_generate_visible_surface_list_indirect_arg_cs.hlsl");
            pso_bbv_removal_indirect_arg_build_ = CreateComputePSO("srvs/bbv_removal_indirect_arg_build_cs.hlsl");
            pso_bbv_element_update_ = CreateComputePSO("srvs/bbv_element_update_cs.hlsl");
            pso_bbv_visible_surface_element_update_ = CreateComputePSO("srvs/bbv_visible_surface_element_update_cs.hlsl");

            pso_wcp_clear_ = CreateComputePSO("srvs/wcp_clear_voxel_cs.hlsl");
            pso_wcp_begin_update_ = CreateComputePSO("srvs/wcp_begin_update_cs.hlsl");
            pso_wcp_visible_surface_proc_ = CreateComputePSO("srvs/wcp_screen_space_pass_cs.hlsl");
            pso_wcp_generate_visible_surface_list_indirect_arg_ = CreateComputePSO("srvs/wcp_generate_visible_surface_list_indirect_arg_cs.hlsl");
            pso_wcp_visible_surface_element_update_ = CreateComputePSO("srvs/wcp_visible_surface_element_update_cs.hlsl");
            pso_wcp_coarse_ray_sample_ = CreateComputePSO("srvs/wcp_element_update_cs.hlsl");
            pso_wcp_fill_probe_octmap_atlas_border_ = CreateComputePSO("srvs/wcp_fill_probe_octmap_atlas_border_cs.hlsl");

            pso_ss_probe_clear_ = CreateComputePSO("srvs/ss_probe_clear_cs.hlsl");
            pso_ss_probe_preupdate_ = CreateComputePSO("srvs/ss_probe_preupdate_cs.hlsl");
            pso_ss_probe_update_ = CreateComputePSO("srvs/ss_probe_update_cs.hlsl");
            pso_ss_probe_spatial_filter_ = CreateComputePSO("srvs/ss_probe_spatial_filter_cs.hlsl");
            pso_ss_probe_sh_update_ = CreateComputePSO("srvs/ss_probe_sh_update_cs.hlsl");
            
            
            // デバッグ用PSO.
            {
                pso_bbv_debug_visualize_ = CreateComputePSO("srvs/debug_util/voxel_debug_visualize_cs.hlsl");
                
                {
                    pso_bbv_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                    ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                    {
                        ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                        vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                        vs_load_desc.shader_model_version          = k_shader_model;
                        vs_load_desc.entry_point_name              = "main_vs";
                        auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("srvs/debug_util/voxel_probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("srvs/debug_util/voxel_probe_debug_ps.hlsl"), &ps_load_desc);
                        gpso_desc.ps = &ps_load_handle->data_;
                    }

                    gpso_desc.num_render_targets = 1;
                    gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                    gpso_desc.depth_stencil_state.depth_enable = true;
                    gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                    gpso_desc.depth_stencil_state.depth_write_enable = true;
                    gpso_desc.depth_stencil_state.stencil_enable = false;
                    gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;
                    
                    auto* pso_cache = p_device->GetPipelineStateCache();
                    pso_bbv_debug_probe_ = pso_cache->GetOrCreate(p_device, gpso_desc);
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
                            p_device, NGL_RENDER_SHADER_PATH("srvs/debug_util/probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("srvs/debug_util/probe_debug_ps.hlsl"), &ps_load_desc);
                        gpso_desc.ps = &ps_load_handle->data_;
                    }

                    gpso_desc.num_render_targets = 1;
                    gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                    gpso_desc.depth_stencil_state.depth_enable = true;
                    gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                    gpso_desc.depth_stencil_state.depth_write_enable = true;
                    gpso_desc.depth_stencil_state.stencil_enable = false;
                    gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;

                    auto* pso_cache = p_device->GetPipelineStateCache();
                    pso_wcp_debug_probe_ = pso_cache->GetOrCreate(p_device, gpso_desc);
                }
            }
        }


        {
            bbv_optional_data_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(BbvOptionalData),
                                               .element_count     = voxel_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default}
                                            ,   "Srvs_BbvOptionalDataBuffer");
        }
        {
            bbv_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = voxel_count * k_bbv_per_voxel_u32_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_BbvBuffer");
        }
        {
            bbv_fine_update_voxel_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = bbv_fine_update_voxel_count_max_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_BbvFineUpdateVoxelList");
        }
        {
            bbv_fine_update_voxel_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_BbvFineUpdateVoxelIndirectArg");
        }
        {
            // 1F更新可能プローブ数分の k_probe_octmap_width*k_probe_octmap_width テクセル分バッファ.
            bbv_fine_update_voxel_probe_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(float),
                                               .element_count     = bbv_fine_update_voxel_count_max_ * (k_probe_octmap_width*k_probe_octmap_width),

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_FLOAT
                                        ,   "Srvs_BbvFineUpdateVoxelProbeBuffer");
        }
        
        {
            bbv_removal_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = (bbv_hollow_voxel_list_count_max_+1) * k_component_count_RemoveVoxelList,// 0番目にアトミックカウンタ用途.　格納情報にuint2相当が必要且つAtomic操作のために2倍サイズのScalarバッファとしている.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_BbvRemovalList");
        }
        
        {
            bbv_removal_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_BbvRemovalIndirectArg");
        }

        {
            // wcp_buffer_初期化.
            wcp_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(WcpProbeData),
                                               .element_count     = wcp_probe_cell_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default}
                                            ,   "Srvs_WcpBuffer");
        }
        {
            wcp_visible_surface_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = wcp_visible_surface_buffer_size_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_WcpVisibleSurfaceList");
        }
        {
            wcp_visible_surface_list_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                               .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_WcpVisibleSurfaceListIndirectArg");
        }

        // WCP プローブアトラス.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  wcp_grid_updater_.Get().flatten_2d_width * k_probe_octmap_width_with_border;
            desc.height = static_cast<u32>(std::ceil((wcp_grid_updater_.Get().total_count + wcp_grid_updater_.Get().flatten_2d_width - 1) / wcp_grid_updater_.Get().flatten_2d_width)) * k_probe_octmap_width_with_border;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16_FLOAT;
            //desc.format = rhi::EResourceFormat::Format_R8_UNORM;
            //desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;// Enhanced Barrier移行時はCommonのみ許可.

            wcp_probe_atlas_tex_.Initialize(p_device, desc, "Srvs_WcpProbeAtlasTex");
        }

        // ScreenSpaceProbeテクスチャ. 解像度固定かつシングルビュー用のテスト.
        const int ss_probe_base_resolution_x = 1920;
        const int ss_probe_base_resolution_y = 1080;
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  ss_probe_base_resolution_x;
            desc.height = ss_probe_base_resolution_y;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;// Enhanced Barrier移行時はCommonのみ許可.

            ss_probe_tex_[i].Initialize(p_device, desc, (0 == i)? "Srvs_SsProbeTexA" : "Srvs_SsProbeTexB");
        }
        // Screen Space Probe Tile Info テクスチャ.
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;// Enhanced Barrier移行時はCommonのみ許可.

            ss_probe_tile_info_tex_[i].Initialize(p_device, desc, (0 == i)? "Srvs_SsProbeTileInfoTexA" : "Srvs_SsProbeTileInfoTexB");
        }
        // Screen Space Probe SH テクスチャ.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            ss_probe_sh_tex_.Initialize(p_device, desc, "Srvs_SsProbeShTex");
        }
        // Screen Space Probe Side Cache テクスチャ.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  ss_probe_base_resolution_x;
            desc.height = ss_probe_base_resolution_y;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            ss_probe_side_cache_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheTex");
        }
        // Screen Space Probe Side Cache メタ情報テクスチャ.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32G32B32A32_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            ss_probe_side_cache_meta_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheMetaTex");
        }
        // Screen Space Probe Side Cache Tile Lock テクスチャ.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_TILE_SIZE -1) / SCREEN_SPACE_PROBE_TILE_SIZE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32_UINT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            ss_probe_side_cache_lock_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheLockTex");
        }

        return true;
    }

    void BitmaskBrickVoxelGi::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        important_point_ = pos;
        important_dir_   = dir;
    }

    
    void BitmaskBrickVoxelGi::Dispatch_Begin(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, const math::Vec2i& render_resolution
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Begin");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const bool is_first_dispatch = is_first_dispatch_;
        is_first_dispatch_           = false;
        ++frame_count_;

        ss_probe_prev_frame_tex_index_ = ss_probe_curr_frame_tex_index_;
        ss_probe_curr_frame_tex_index_ = 1 - ss_probe_prev_frame_tex_index_;
        ss_probe_latest_filtered_frame_tex_index_ = ss_probe_prev_frame_tex_index_;

        ss_probe_tile_info_prev_frame_tex_index_ = ss_probe_tile_info_curr_frame_tex_index_;
        ss_probe_tile_info_curr_frame_tex_index_ = 1 - ss_probe_tile_info_prev_frame_tex_index_;


        // 重視位置を若干補正.
        #if 0
            const math::Vec3 modified_important_point = important_point_ + important_dir_ * 5.0f;
        #else
            const math::Vec3 modified_important_point = important_point_;
        #endif

        
        #if 1
        {
            bbv_grid_updater_.UpdateGrid(modified_important_point);
            wcp_grid_updater_.UpdateGrid(modified_important_point);
        }
        #else
            // FIXME. デバッグ. gridの移動を止めて外部からレイトレースをした場合のデバッグ等.
        #endif

        const math::Vec2i hw_depth_size = render_resolution;

        cbh_dispatch_ = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SrvsParam));
        {
            auto* p = cbh_dispatch_->buffer.MapAs<SrvsParam>();

            //Bbv
            {
                p->bbv.grid_resolution = bbv_grid_updater_.Get().resolution.Cast<int>();
                p->bbv.grid_min_pos     = bbv_grid_updater_.Get().min_pos;
                p->bbv.grid_min_voxel_coord = math::Vec3::Floor(bbv_grid_updater_.Get().min_pos * (1.0f / bbv_grid_updater_.Get().cell_size)).Cast<int>();

                p->bbv.grid_toroidal_offset =  bbv_grid_updater_.Get().toroidal_offset;
                p->bbv.grid_toroidal_offset_prev =  bbv_grid_updater_.Get().toroidal_offset_prev;

                p->bbv.grid_move_cell_delta = bbv_grid_updater_.Get().min_pos_delta_cell;

                p->bbv.flatten_2d_width = bbv_grid_updater_.Get().flatten_2d_width;

                p->bbv.cell_size       = bbv_grid_updater_.Get().cell_size;
                p->bbv.cell_size_inv    = 1.0f / bbv_grid_updater_.Get().cell_size;

                p->bbv_indirect_cs_thread_group_size = math::Vec3i(pso_bbv_visible_surface_element_update_->GetThreadGroupSizeX(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeY(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeZ());
                p->bbv_visible_voxel_buffer_size = bbv_fine_update_voxel_count_max_;
                p->bbv_hollow_voxel_buffer_size = bbv_hollow_voxel_list_count_max_;
            }
            // Wcp
            {
                p->wcp.grid_resolution = wcp_grid_updater_.Get().resolution.Cast<int>();
                p->wcp.grid_min_pos     = wcp_grid_updater_.Get().min_pos;
                p->wcp.grid_min_voxel_coord = math::Vec3::Floor(wcp_grid_updater_.Get().min_pos * (1.0f / wcp_grid_updater_.Get().cell_size)).Cast<int>();

                p->wcp.grid_toroidal_offset =  wcp_grid_updater_.Get().toroidal_offset;
                p->wcp.grid_toroidal_offset_prev =  wcp_grid_updater_.Get().toroidal_offset_prev;

                p->wcp.grid_move_cell_delta = wcp_grid_updater_.Get().min_pos_delta_cell;

                p->wcp.flatten_2d_width = wcp_grid_updater_.Get().flatten_2d_width;

                p->wcp.cell_size       = wcp_grid_updater_.Get().cell_size;
                p->wcp.cell_size_inv    = 1.0f / wcp_grid_updater_.Get().cell_size;

                p->wcp_indirect_cs_thread_group_size = math::Vec3i(pso_wcp_visible_surface_element_update_->GetThreadGroupSizeX(), pso_wcp_visible_surface_element_update_->GetThreadGroupSizeY(), pso_wcp_visible_surface_element_update_->GetThreadGroupSizeZ());
                p->wcp_visible_voxel_buffer_size = wcp_visible_surface_buffer_size_;
            }

            p->tex_main_view_depth_size = hw_depth_size;
            p->frame_count = frame_count_;

            p->ss_probe_temporal_update_group_size = k_ss_probe_update_skip_tile_group_width;
            p->ss_probe_ray_start_offset_scale = k_ss_probe_ray_start_offset_scale;
            p->ss_probe_ray_normal_offset_scale = k_ss_probe_ray_normal_offset_scale;
            p->ss_probe_spatial_filter_normal_cos_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_normal_cos_threshold_;
            p->ss_probe_spatial_filter_depth_exp_scale = ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_depth_exp_scale_;
            p->ss_probe_temporal_min_hysteresis = k_ss_probe_temporal_min_hysteresis;
            p->ss_probe_temporal_max_hysteresis = k_ss_probe_temporal_max_hysteresis;
            p->ss_probe_temporal_reprojection_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_reprojection_enable_;
            p->ss_probe_ray_guiding_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_ray_guiding_enable_;
            p->ss_probe_side_cache_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_enable_;
            p->ss_probe_side_cache_max_life_frame = k_ss_probe_side_cache_max_life_frame;
            p->ss_probe_preupdate_relocation_probability = ScreenReconstructedVoxelStructure::dbg_ss_probe_preupdate_relocation_probability_;
            p->ss_probe_temporal_filter_normal_cos_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_normal_cos_threshold_;
            p->ss_probe_temporal_filter_plane_dist_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_plane_dist_threshold_;
            p->ss_probe_side_cache_plane_dist_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_plane_dist_threshold_;

            p->main_light_dir_ws = main_view_info.main_light_dir_ws;

            p->debug_view_mode = ScreenReconstructedVoxelStructure::dbg_view_mode_;
            p->debug_bbv_probe_mode = ScreenReconstructedVoxelStructure::dbg_bbv_probe_debug_mode_;
            p->debug_wcp_probe_mode = ScreenReconstructedVoxelStructure::dbg_wcp_probe_debug_mode_;

            p->debug_probe_radius = ScreenReconstructedVoxelStructure::dbg_probe_scale_ * 0.5f * bbv_grid_updater_.Get().cell_size / k_bbv_per_voxel_resolution;
            p->debug_probe_near_geom_scale = ScreenReconstructedVoxelStructure::dbg_probe_near_geom_scale_;

            cbh_dispatch_->buffer.Unmap();
        }
        // 初回クリア.
        if (is_first_dispatch)
        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvInitClear");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_clear_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
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
                pso_wcp_clear_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_wcp_clear_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                pso_wcp_clear_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_uav.Get(), wcp_probe_atlas_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_wcp_clear_.Get());
                p_command_list->SetDescriptorSet(pso_wcp_clear_.Get(), &desc_set);
                pso_wcp_clear_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                p_command_list->ResourceBarrier(wcp_probe_atlas_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
            }

            // SsProbeクリア. pso_ss_probe_clear_使用.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "SsProbeInitClear");
                p_command_list->SetPipelineState(pso_ss_probe_clear_.Get());
                for(int i = 0; i < 2; ++i)
                {
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_ss_probe_clear_->SetView(&desc_set, k_shader_bind_name_ssprobe_uav.Get(), ss_probe_tex_[i].uav.Get());
                    pso_ss_probe_clear_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_tile_info_tex_[i].uav.Get());
                    p_command_list->SetDescriptorSet(pso_ss_probe_clear_.Get(), &desc_set);
                    pso_ss_probe_clear_->DispatchHelper(p_command_list, ss_probe_tex_[i].texture->GetWidth(), ss_probe_tex_[i].texture->GetHeight(), 1);

                    p_command_list->ResourceBarrier(ss_probe_tex_[i].texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(ss_probe_tile_info_tex_[i].texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                }
                {
                    // Side cache clears use the same clear kernel by rebinding UAVs.
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_ss_probe_clear_->SetView(&desc_set, k_shader_bind_name_ssprobe_uav.Get(), ss_probe_side_cache_tex_.uav.Get());
                    pso_ss_probe_clear_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_side_cache_meta_tex_.uav.Get());
                    p_command_list->SetDescriptorSet(pso_ss_probe_clear_.Get(), &desc_set);
                    pso_ss_probe_clear_->DispatchHelper(p_command_list, ss_probe_side_cache_tex_.texture->GetWidth(), ss_probe_side_cache_tex_.texture->GetHeight(), 1);

                    p_command_list->ResourceBarrier(ss_probe_side_cache_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(ss_probe_side_cache_meta_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(ss_probe_side_cache_lock_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                }
                p_command_list->ResourceBarrier(ss_probe_sh_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
            }
        }
        // Bbv Begin Update Pass.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_begin_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());
            pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_begin_update_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_begin_update_.Get(), &desc_set);
            pso_bbv_begin_update_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
        }
    }

    void BitmaskBrickVoxelGi::Dispatch_Bbv_OccupancyUpdate_View(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info,
            
                        const InjectionSourceDepthBufferInfo& depth_buffer_info
    )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Dispatch_Bbv_OccupancyUpdate_View");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const int num_depth_buffer = 1 + static_cast<int>(depth_buffer_info.sub_array.size());
        for(int i = 0; i < num_depth_buffer; ++i)
        {
            #if 1
                // 最期にPrimaryを実行するように順序入れ替え. Primaryで可視な表面のOccupancy Updateが最優先になるようにするため.
                const InjectionSourceDepthBufferViewInfo& target_depth_info = (i == (num_depth_buffer - 1)) ? depth_buffer_info.primary : depth_buffer_info.sub_array[i];
            #else
                // 0番はPrimary, それ以降はSubかを参照.
                const InjectionSourceDepthBufferViewInfo& target_depth_info = (i == 0) ? depth_buffer_info.primary : depth_buffer_info.sub_array[i - 1];
            #endif
            
            if(!target_depth_info.is_enable_injection_pass && !target_depth_info.is_enable_removal_pass)
                continue;

            auto cbh_injection_view_info = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(BbvSurfaceInjectionViewInfo));
            {
                auto* p = cbh_injection_view_info->buffer.MapAs<BbvSurfaceInjectionViewInfo>();
                {
                    p->cb_view_mtx = target_depth_info.view_mat;
                    p->cb_proj_mtx = target_depth_info.proj_mat;
                    p->cb_view_inv_mtx = ngl::math::Mat34::Inverse(target_depth_info.view_mat);
                    p->cb_proj_inv_mtx = ngl::math::Mat44::Inverse(target_depth_info.proj_mat);
                    p->cb_ndc_z_to_view_z_coef =  CalcViewDepthReconstructCoefFromProjectionMatrix(target_depth_info.proj_mat);
                    // ViewDepthBufferの他, ShadowMapによるInjectionもしたいのでShadowMapAtlas用にオフセット考慮.
                    p->cb_view_depth_buffer_offset_size = math::Vec4i(
                        target_depth_info.atlas_offset.x,
                        target_depth_info.atlas_offset.y,
                        target_depth_info.atlas_resolution.x,
                        target_depth_info.atlas_resolution.y
                    );
                }
                cbh_injection_view_info->buffer.Unmap();
            }

            // Bbv Begin View Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginViewUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_begin_view_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_bbv_begin_view_update_->SetView(&desc_set, "RWVisibleVoxelList", bbv_fine_update_voxel_list_.uav.Get());
                pso_bbv_begin_view_update_->SetView(&desc_set, "RWRemoveVoxelList", bbv_removal_list_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_begin_view_update_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_begin_view_update_.Get(), &desc_set);
                pso_bbv_begin_view_update_->DispatchHelper(p_command_list, 1, 1, 1);

                p_command_list->ResourceUavBarrier(bbv_fine_update_voxel_list_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_removal_list_.buffer.Get());
            }

            // Removal Pass Lambda.
            auto func_call_removal_pass = [this](
                rhi::GraphicsCommandListDep* p_command_list,
                rhi::ConstantBufferPooledHandle scene_cbv,
                rhi::ConstantBufferPooledHandle cbh_injection_view_info,
                const InjectionSourceDepthBufferViewInfo& target_depth_info
            )
            {
                // Bbv Removal Pass.
                if(target_depth_info.is_enable_removal_pass)
                {
                    // Bbv Removal List Build.
                    // 動的な環境で中空になった可能性のあるBbvをクリアするためのリスト生成. Depthからその表面に至るまでの経路上のVoxelが中空であると仮定してリスト化.
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBuildRemovalList");

                        ngl::rhi::DescriptorSetDep desc_set = {};
                        pso_bbv_removal_list_build_->SetView(&desc_set, "TexHardwareDepth", target_depth_info.hw_depth_srv.Get());
                        pso_bbv_removal_list_build_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                        pso_bbv_removal_list_build_->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
                        pso_bbv_removal_list_build_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                        pso_bbv_removal_list_build_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                        pso_bbv_removal_list_build_->SetView(&desc_set, "RWRemoveVoxelList", bbv_removal_list_.uav.Get());

                        p_command_list->SetPipelineState(pso_bbv_removal_list_build_.Get());
                        p_command_list->SetDescriptorSet(pso_bbv_removal_list_build_.Get(), &desc_set);
                        pso_bbv_removal_list_build_->DispatchHelper(p_command_list, target_depth_info.atlas_resolution.x, target_depth_info.atlas_resolution.y, 1);  // Screen処理でDispatch.
                        p_command_list->ResourceUavBarrier(bbv_removal_list_.buffer.Get());
                    }
                    // RemoveVoxelListのIndirectArg生成.
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBuildRemovalIndirectArg");

                        bbv_removal_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                        ngl::rhi::DescriptorSetDep desc_set = {};
                        pso_bbv_removal_indirect_arg_build_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                        pso_bbv_removal_indirect_arg_build_->SetView(&desc_set, "RemoveVoxelList", bbv_removal_list_.srv.Get());
                        pso_bbv_removal_indirect_arg_build_->SetView(&desc_set, "RWRemoveVoxelIndirectArg", bbv_removal_indirect_arg_.uav.Get());

                        p_command_list->SetPipelineState(pso_bbv_removal_indirect_arg_build_.Get());
                        p_command_list->SetDescriptorSet(pso_bbv_removal_indirect_arg_build_.Get(), &desc_set);
                        pso_bbv_removal_indirect_arg_build_->DispatchHelper(p_command_list, 1, 1, 1);

                        bbv_removal_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
                    }
                    // リストに則って実際に除去するパス.
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvApplyRemoval");

                        ngl::rhi::DescriptorSetDep desc_set = {};
                        pso_bbv_removal_apply_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                        pso_bbv_removal_apply_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
                        pso_bbv_removal_apply_->SetView(&desc_set, "RemoveVoxelList", bbv_removal_list_.srv.Get());
                        p_command_list->SetPipelineState(pso_bbv_removal_apply_.Get());
                        p_command_list->SetDescriptorSet(pso_bbv_removal_apply_.Get(), &desc_set);
                        p_command_list->DispatchIndirect(bbv_removal_indirect_arg_.buffer.Get());

                        p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
                    }
                }
            };
            // Injection Pass Lambda.
            auto func_call_injection_pass = [this](
                rhi::GraphicsCommandListDep* p_command_list,
                rhi::ConstantBufferPooledHandle scene_cbv,
                rhi::ConstantBufferPooledHandle cbh_injection_view_info,
                const InjectionSourceDepthBufferViewInfo& target_depth_info
            )
            {
                // Bbv Injection Pass.
                if(target_depth_info.is_enable_injection_pass)
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvApplyInjection");

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_bbv_injection_apply_->SetView(&desc_set, "TexHardwareDepth", target_depth_info.hw_depth_srv.Get());
                    
                    pso_bbv_injection_apply_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                    pso_bbv_injection_apply_->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
                    pso_bbv_injection_apply_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);

                    pso_bbv_injection_apply_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
                    pso_bbv_injection_apply_->SetView(&desc_set, "RWVisibleVoxelList", bbv_fine_update_voxel_list_.uav.Get());

                    p_command_list->SetPipelineState(pso_bbv_injection_apply_.Get());
                    p_command_list->SetDescriptorSet(pso_bbv_injection_apply_.Get(), &desc_set);
                    pso_bbv_injection_apply_->DispatchHelper(p_command_list, target_depth_info.atlas_resolution.x, target_depth_info.atlas_resolution.y, 1);  // Screen処理でDispatch.

                    p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(bbv_fine_update_voxel_list_.buffer.Get());
                }
            };

            #if 1
                // Removal Pass -> Injection Pass の順序.
                func_call_removal_pass(p_command_list, scene_cbv, cbh_injection_view_info, target_depth_info);
                func_call_injection_pass(p_command_list, scene_cbv, cbh_injection_view_info, target_depth_info);
            #else
                // Injection Pass -> Removal Pass の順序.
                func_call_injection_pass(p_command_list, scene_cbv, cbh_injection_view_info, target_depth_info);
                func_call_removal_pass(p_command_list, scene_cbv, cbh_injection_view_info, target_depth_info);
            #endif
        }

        // ここから先はDebugBuffer数に依らず実行.
        // 可視表面VoxelリストはPrimary優先で詰め込まれている.

        // VisibleVoxel IndirectArg生成.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "GenerateVisibleElementIndirectArg");
            
            bbv_fine_update_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "VisibleVoxelList", bbv_fine_update_voxel_list_.srv.Get());
            pso_bbv_generate_visible_voxel_indirect_arg_->SetView(&desc_set, "RWVisibleVoxelIndirectArg", bbv_fine_update_voxel_indirect_arg_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_generate_visible_voxel_indirect_arg_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_generate_visible_voxel_indirect_arg_.Get(), &desc_set);
            pso_bbv_generate_visible_voxel_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

            bbv_fine_update_voxel_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
        }

        // Visible Surface Voxel Update Pass.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvVisibleSurfaceVoxelUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_visible_surface_element_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_visible_surface_element_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
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
    
    void BitmaskBrickVoxelGi::Dispatch_Bbv_Main(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Bbv_Main");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        // Voxel Update.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvCommonUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_element_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_element_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_element_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_element_update_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_element_update_.Get(), &desc_set);
            pso_bbv_element_update_->DispatchHelper(p_command_list, (bbv_grid_updater_.Get().total_count + (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT)) / (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT+1), 1, 1);

            p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
        }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_Wcp(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Wcp");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));
        const ngl::u32 ss_probe_history_index = ss_probe_prev_frame_tex_index_;
        const ngl::u32 ss_probe_update_write_index = ss_probe_curr_frame_tex_index_;
        const ngl::u32 ss_probe_tile_info_history_index = ss_probe_tile_info_prev_frame_tex_index_;
        const ngl::u32 ss_probe_tile_info_curr_index = ss_probe_tile_info_curr_frame_tex_index_;
        const bool is_ss_probe_spatial_filter_enable = (0 != ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_enable_);

        ngl::u32 ss_probe_sh_input_index = ss_probe_update_write_index;

        // ScreenSpaceProbe.
        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbePreUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_ss_probe_preupdate_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_ss_probe_preupdate_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_ss_probe_preupdate_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_preupdate_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_preupdate_.Get(), &desc_set);
                // 1/8 解像度のProbe単位Texel更新.
                pso_ss_probe_preupdate_->DispatchHelper(p_command_list, ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture->GetWidth(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture->GetHeight(), 1);

                p_command_list->ResourceUavBarrier(ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbeUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                //pso_ss_probe_update_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_ss_probe_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_ss_probe_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].uav.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_history_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_history_index].srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_history_srv.Get(), ss_probe_tex_[ss_probe_history_index].srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_uav.Get(), ss_probe_tex_[ss_probe_update_write_index].uav.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_srv.Get(), ss_probe_side_cache_tex_.srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_uav.Get(), ss_probe_side_cache_tex_.uav.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_meta_srv.Get(), ss_probe_side_cache_meta_tex_.srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_meta_uav.Get(), ss_probe_side_cache_meta_tex_.uav.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_lock_uav.Get(), ss_probe_side_cache_lock_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_update_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_update_.Get(), &desc_set);

                pso_ss_probe_update_->DispatchHelper(p_command_list, ss_probe_tex_[ss_probe_update_write_index].texture->GetWidth()/k_ss_probe_update_skip_tile_group_width, ss_probe_tex_[ss_probe_update_write_index].texture->GetHeight()/k_ss_probe_update_skip_tile_group_width, 1);

                p_command_list->ResourceUavBarrier(ss_probe_tex_[ss_probe_update_write_index].texture.Get());
                p_command_list->ResourceUavBarrier(ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture.Get());
                p_command_list->ResourceUavBarrier(ss_probe_side_cache_tex_.texture.Get());
                p_command_list->ResourceUavBarrier(ss_probe_side_cache_meta_tex_.texture.Get());
                p_command_list->ResourceUavBarrier(ss_probe_side_cache_lock_tex_.texture.Get());
            }
            if(is_ss_probe_spatial_filter_enable)
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbeSpatialFilter");

                const ngl::u32 ss_probe_filter_input_index = ss_probe_update_write_index;
                const ngl::u32 ss_probe_filter_output_index = 1 - ss_probe_filter_input_index;

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_ss_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_ssprobe_srv.Get(), ss_probe_tex_[ss_probe_filter_input_index].srv.Get());
                pso_ss_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].srv.Get());
                pso_ss_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_ssprobe_filtered_uav.Get(), ss_probe_tex_[ss_probe_filter_output_index].uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_spatial_filter_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_spatial_filter_.Get(), &desc_set);
                pso_ss_probe_spatial_filter_->DispatchHelper(
                    p_command_list,
                    ss_probe_tex_[ss_probe_filter_output_index].texture->GetWidth(),
                    ss_probe_tex_[ss_probe_filter_output_index].texture->GetHeight(),
                    1);

                p_command_list->ResourceUavBarrier(ss_probe_tex_[ss_probe_filter_output_index].texture.Get());

                // SpatialFilter後のフリップで、最新フィルタ済みを公開/次フレーム履歴として扱う.
                ss_probe_latest_filtered_frame_tex_index_ = ss_probe_filter_output_index;
                ss_probe_curr_frame_tex_index_ = ss_probe_latest_filtered_frame_tex_index_;
                ss_probe_prev_frame_tex_index_ = 1 - ss_probe_curr_frame_tex_index_;

                ss_probe_sh_input_index = ss_probe_latest_filtered_frame_tex_index_;
            }
            else
            {
                // SpatialFilter無効時はDispatchとフリップを行わず、Update出力をそのまま利用する.
                ss_probe_latest_filtered_frame_tex_index_ = ss_probe_update_write_index;
                ss_probe_sh_input_index = ss_probe_update_write_index;
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbeShUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_ss_probe_sh_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_ss_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_srv.Get(), ss_probe_tex_[ss_probe_sh_input_index].srv.Get());
                pso_ss_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].srv.Get());
                pso_ss_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_sh_uav.Get(), ss_probe_sh_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_sh_update_.Get(), &desc_set);
                pso_ss_probe_sh_update_->DispatchHelper(p_command_list, ss_probe_sh_tex_.texture->GetWidth(), ss_probe_sh_tex_.texture->GetHeight(), 1);

                p_command_list->ResourceUavBarrier(ss_probe_sh_tex_.texture.Get());
            }
        }

        // WCP.
        {
            // Wcp Begin Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpBeginUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_wcp_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_wcp_begin_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_wcp_begin_update_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                pso_wcp_begin_update_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_uav.Get(), wcp_probe_atlas_tex_.uav.Get());
                pso_wcp_begin_update_->SetView(&desc_set, "RWSurfaceProbeCellList", wcp_visible_surface_list_.uav.Get());

                p_command_list->SetPipelineState(pso_wcp_begin_update_.Get());
                p_command_list->SetDescriptorSet(pso_wcp_begin_update_.Get(), &desc_set);
                pso_wcp_begin_update_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
                p_command_list->ResourceUavBarrier(wcp_visible_surface_list_.buffer.Get());
            }
            
            // Wcp Visible Surface Processing Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpVisibleSurfaceProcessing");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_wcp_visible_surface_proc_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_wcp_visible_surface_proc_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_wcp_visible_surface_proc_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
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
                pso_wcp_generate_visible_surface_list_indirect_arg_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
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
                pso_wcp_visible_surface_element_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_wcp_visible_surface_element_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_wcp_visible_surface_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                pso_wcp_visible_surface_element_update_->SetView(&desc_set, "SurfaceProbeCellList", wcp_visible_surface_list_.srv.Get());
                pso_wcp_visible_surface_element_update_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                pso_wcp_visible_surface_element_update_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_uav.Get(), wcp_probe_atlas_tex_.uav.Get());


                p_command_list->SetPipelineState(pso_wcp_visible_surface_element_update_.Get());
                p_command_list->SetDescriptorSet(pso_wcp_visible_surface_element_update_.Get(), &desc_set);

                p_command_list->DispatchIndirect(wcp_visible_surface_list_indirect_arg_.buffer.Get());// 可視SurfaceListDispatch.


                p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
            }
            // Wcp Coarse Probe RaySample Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpCoarseProbeRaySample");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_wcp_coarse_ray_sample_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_wcp_coarse_ray_sample_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_wcp_coarse_ray_sample_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                pso_wcp_coarse_ray_sample_->SetView(&desc_set, "RWWcpProbeBuffer", wcp_buffer_.uav.Get());
                pso_wcp_coarse_ray_sample_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_uav.Get(), wcp_probe_atlas_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_wcp_coarse_ray_sample_.Get());
                p_command_list->SetDescriptorSet(pso_wcp_coarse_ray_sample_.Get(), &desc_set);
                // 全Probe更新のスキップ要素分考慮したDispatch.
                pso_wcp_coarse_ray_sample_->DispatchHelper(p_command_list, (wcp_grid_updater_.Get().total_count + (WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT)) / (WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT+1), 1, 1);

                p_command_list->ResourceUavBarrier(wcp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
            }
            // Wcp Octahedral Map Border Fill Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpFillProbeOctmapAtlasBorder");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_wcp_fill_probe_octmap_atlas_border_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_wcp_fill_probe_octmap_atlas_border_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_uav.Get(), wcp_probe_atlas_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_wcp_fill_probe_octmap_atlas_border_.Get());
                p_command_list->SetDescriptorSet(pso_wcp_fill_probe_octmap_atlas_border_.Get(), &desc_set);

                // 全Probe更新のスキップ要素分考慮したDispatch.
                pso_wcp_fill_probe_octmap_atlas_border_->DispatchHelper(p_command_list, wcp_grid_updater_.Get().total_count, 1, 1);

                p_command_list->ResourceUavBarrier(wcp_probe_atlas_tex_.texture.Get());
            }
        }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_Debug(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Debug");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        // デバッグ描画準備.
        if(0 <= ScreenReconstructedVoxelStructure::dbg_view_mode_)
        {
            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_srv.Get(), wcp_probe_atlas_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_srv.Get(), ss_probe_tex_[ss_probe_latest_filtered_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_sh_srv.Get(), ss_probe_sh_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_srv.Get(), ss_probe_side_cache_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_meta_srv.Get(), ss_probe_side_cache_meta_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());
            
            pso_bbv_debug_visualize_->SetView(&desc_set, "RWTexWork", work_uav.Get());

            p_command_list->SetPipelineState(pso_bbv_debug_visualize_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_debug_visualize_.Get(), &desc_set);

            pso_bbv_debug_visualize_->DispatchHelper(p_command_list, work_tex_size.x, work_tex_size.y, 1);
        }
    }

    void BitmaskBrickVoxelGi::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Debug");

        
        // Viewport.
        gfx::helper::SetFullscreenViewportAndScissor(p_command_list, lighting_tex->GetWidth(), lighting_tex->GetHeight());

        // Rtv, Dsv セット.
        {
            const auto* p_rtv = lighting_rtv.Get();
            p_command_list->SetRenderTargets(&p_rtv, 1, hw_depth_dsv.Get());
        }

        if (0 <= ScreenReconstructedVoxelStructure::dbg_bbv_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvProbeDebug");

            p_command_list->SetPipelineState(pso_bbv_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_bbv_debug_probe_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            
            pso_bbv_debug_probe_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_probe_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_bbv_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * bbv_grid_updater_.Get().total_count, 1, 0, 0);
        }
        if (0 <= ScreenReconstructedVoxelStructure::dbg_wcp_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "WcpProbeDebug");

            p_command_list->SetPipelineState(pso_wcp_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_wcp_debug_probe_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);

            pso_wcp_debug_probe_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_wcp_debug_probe_->SetView(&desc_set, "WcpProbeBuffer", wcp_buffer_.srv.Get());
            pso_wcp_debug_probe_->SetView(&desc_set, k_shader_bind_name_wcp_atlas_srv.Get(), wcp_probe_atlas_tex_.srv.Get());
            pso_wcp_debug_probe_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_wcp_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * wcp_grid_updater_.Get().total_count, 1, 0, 0);
        }

    }


    // ----------------------------------------------------------------

    
    ScreenReconstructedVoxelStructure::~ScreenReconstructedVoxelStructure()
    {
        Finalize();
    }

    // 初期化
    bool ScreenReconstructedVoxelStructure::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u wcp_resolution, float wcp_cell_size)
    {
        bbvgi_instance_ = new BitmaskBrickVoxelGi();
        BitmaskBrickVoxelGi::InitArg init_arg = {};
        {
            init_arg.voxel_resolution = bbv_resolution;
            init_arg.voxel_size       = bbv_cell_size;

            init_arg.probe_resolution = wcp_resolution;
            init_arg.probe_cell_size  = wcp_cell_size;
        }
        if(!bbvgi_instance_->Initialize(p_device, init_arg))
        {
            delete bbvgi_instance_;
            bbvgi_instance_ = nullptr;
            return false;
        }

        is_initialized_ = true;
        return true;
    }
    // 破棄
    void ScreenReconstructedVoxelStructure::Finalize()
    {
        if(bbvgi_instance_)
        {
            delete bbvgi_instance_;
            bbvgi_instance_ = nullptr;
        }
        is_initialized_ = false;
    }

    void ScreenReconstructedVoxelStructure::DispatchBegin(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, const math::Vec2i& render_resolution)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Begin(p_command_list, scene_cbv, main_view_info, render_resolution);
        }
    }
    void ScreenReconstructedVoxelStructure::DispatchViewBbvOccupancyUpdate(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, 
        const InjectionSourceDepthBufferInfo& depth_buffer_info)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_OccupancyUpdate_View(p_command_list, scene_cbv, main_view_info, depth_buffer_info);
        }
    }
    void ScreenReconstructedVoxelStructure::DispatchUpdate(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_Main(p_command_list, scene_cbv);
            bbvgi_instance_->Dispatch_Wcp(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
            bbvgi_instance_->Dispatch_Debug(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv, work_tex, work_uav);
        }
    }

    void ScreenReconstructedVoxelStructure::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->DebugDraw(p_command_list, scene_cbv, hw_depth_tex, hw_depth_dsv, lighting_tex, lighting_rtv);
        }
    }

    void ScreenReconstructedVoxelStructure::SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->SetImportantPointInfo(pos, dir);
        }
    }

    void ScreenReconstructedVoxelStructure::SetDescriptor(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const
    {
        assert(bbvgi_instance_);
        p_pso->SetView(p_desc_set, k_shader_bind_name_wcp_atlas_srv.Get(), bbvgi_instance_->GetWcpProbeAtlasTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_srv.Get(), bbvgi_instance_->GetSsProbeTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), bbvgi_instance_->GetSsProbeTileInfoTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_sh_srv.Get(), bbvgi_instance_->GetSsProbeShTex().Get());
        p_pso->SetView(p_desc_set, "cb_srvs", &bbvgi_instance_->GetDispatchCbh()->cbv);
    }

}  // namespace ngl::render::app
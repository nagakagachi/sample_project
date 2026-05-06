/*
    srvs.cpp
    screen-reconstructed voxel structure.
*/

#include "render/app/srvs/srvs.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

#include "gfx/command_helper.h"
#include "gfx/rendering/global_render_resource.h"
#include "gfx/rtg/graph_builder.h"
#include "gfx/rtg/rtg_common.h"
#include "resource/resource_manager.h"
#include "imgui/imgui_interface.h"


namespace ngl::render::app
{
    
    #define NGL_SHADER_CPP_INCLUDE
    // cpp/hlsl共通定義用ヘッダ.
    #include "../shader/srvs/srvs_common_header.hlsli"
    #undef NGL_SHADER_CPP_INCLUDE


    static constexpr size_t k_sizeof_BbvOptionalData = sizeof(BbvOptionalData);
    static constexpr size_t k_sizeof_FspProbeData      = sizeof(FspProbeData);
    static constexpr size_t k_sizeof_FspProbePoolData  = sizeof(FspProbePoolData);
    static constexpr u32 k_max_update_probe_work_count = 1024;

    static constexpr u32 k_fsp_probe_pool_size = 1<<13;//10000;
    static constexpr u32 k_fsp_probe_surface_cell_count_max = 1024*2;//1024;
    
    
    // 時間分散するScreenProbeグループのサイズ. 幅がこのサイズのProbeグループ毎に1Fに一つ更新をする. GI-1.0などは2を指定して 4フレームで2x2のグループが更新される.
    static const int k_ss_probe_update_skip_tile_group_width = 1;

    static math::Vec2u CalcBbvRadianceInjectionDispatchResolution(const math::Vec2u& src_resolution)
    {
        const math::Vec2u tile_grid_resolution(
            (src_resolution.x + (k_bbv_radiance_injection_tile_width - 1u)) / k_bbv_radiance_injection_tile_width,
            (src_resolution.y + (k_bbv_radiance_injection_tile_width - 1u)) / k_bbv_radiance_injection_tile_width);
        // radiance injection は全 screen tile を起動せず、2x2 group 数ぶんだけ threadgroup を起動する。
        const math::Vec2u group_grid_resolution(
            (tile_grid_resolution.x + (k_bbv_radiance_injection_tile_group_resolution - 1u)) / k_bbv_radiance_injection_tile_group_resolution,
            (tile_grid_resolution.y + (k_bbv_radiance_injection_tile_group_resolution - 1u)) / k_bbv_radiance_injection_tile_group_resolution);
        return math::Vec2u(
            group_grid_resolution.x * k_bbv_radiance_injection_tile_width,
            group_grid_resolution.y * k_bbv_radiance_injection_tile_width);
    }

    static u32 CalcBbvRadianceResolveDispatchCount(const math::Vec3u& grid_resolution)
    {
        // radiance resolve は Brick 全数 dispatch せず、2x2x2 group 数ぶんだけ起動する。
        const math::Vec3u group_grid_resolution(
            (grid_resolution.x + (k_bbv_radiance_resolve_brick_group_resolution - 1u)) / k_bbv_radiance_resolve_brick_group_resolution,
            (grid_resolution.y + (k_bbv_radiance_resolve_brick_group_resolution - 1u)) / k_bbv_radiance_resolve_brick_group_resolution,
            (grid_resolution.z + (k_bbv_radiance_resolve_brick_group_resolution - 1u)) / k_bbv_radiance_resolve_brick_group_resolution);
        return group_grid_resolution.x * group_grid_resolution.y * group_grid_resolution.z;
    }

    static bool InitializeReadbackBuffer(ngl::rhi::DeviceDep* p_device, ngl::rhi::RefBufferDep& out_buffer, const rhi::BufferDep::Desc& src_desc, const char* debug_name)
    {
        out_buffer.Reset(new rhi::BufferDep());
        rhi::BufferDep::Desc desc = src_desc;
        desc.bind_flag = rhi::ResourceBindFlag::None;
        desc.heap_type = rhi::EResourceHeapType::Readback;
        desc.initial_state = rhi::EResourceState::CopyDst;
        return out_buffer->Initialize(p_device, desc, debug_name);
    }


    // デバッグ.
    int ScreenReconstructedVoxelStructure::dbg_view_category_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_view_sub_mode_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_bbv_probe_debug_mode_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_fsp_probe_debug_mode_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_fsp_probe_debug_cascade_ = -1;
    int ScreenReconstructedVoxelStructure::dbg_fsp_cascade_count_ = 1;
    float ScreenReconstructedVoxelStructure::dbg_probe_scale_ = 1.0f;
    float ScreenReconstructedVoxelStructure::dbg_probe_near_geom_scale_ = 0.2f;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_reprojection_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_ray_guiding_enable_ = 1;
    int ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_enable_ = 1;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_preupdate_relocation_probability_ = k_default_srvs_param.ss_probe_preupdate_relocation_probability;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_normal_cos_threshold_ = k_default_srvs_param.ss_probe_temporal_filter_normal_cos_threshold;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_plane_dist_threshold_ = k_default_srvs_param.ss_probe_temporal_filter_plane_dist_threshold;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_normal_cos_threshold_ = k_default_srvs_param.ss_probe_spatial_filter_normal_cos_threshold;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_depth_exp_scale_ = k_default_srvs_param.ss_probe_spatial_filter_depth_exp_scale;
    float ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_plane_dist_threshold_ = k_default_srvs_param.ss_probe_side_cache_plane_dist_threshold;
    int ScreenReconstructedVoxelStructure::assp_spatial_filter_enable_ = k_default_srvs_param.assp_spatial_filter_enable;
    float ScreenReconstructedVoxelStructure::assp_spatial_filter_normal_cos_threshold_ = k_default_srvs_param.assp_spatial_filter_normal_cos_threshold;
    float ScreenReconstructedVoxelStructure::assp_spatial_filter_depth_exp_scale_ = k_default_srvs_param.assp_spatial_filter_depth_exp_scale;
    int ScreenReconstructedVoxelStructure::assp_temporal_reprojection_enable_ = k_default_srvs_param.assp_temporal_reprojection_enable;
    int ScreenReconstructedVoxelStructure::assp_ray_guiding_enable_ = k_default_srvs_param.assp_ray_guiding_enable;
    int ScreenReconstructedVoxelStructure::assp_ray_budget_min_rays_ = k_default_srvs_param.assp_ray_budget_min_rays;
    int ScreenReconstructedVoxelStructure::assp_ray_budget_max_rays_ = k_default_srvs_param.assp_ray_budget_max_rays;
    float ScreenReconstructedVoxelStructure::assp_ray_budget_variance_weight_ = k_default_srvs_param.assp_ray_budget_variance_weight;
    float ScreenReconstructedVoxelStructure::assp_ray_budget_normal_delta_weight_ = k_default_srvs_param.assp_ray_budget_normal_delta_weight;
    float ScreenReconstructedVoxelStructure::assp_ray_budget_depth_delta_weight_ = k_default_srvs_param.assp_ray_budget_depth_delta_weight;
    float ScreenReconstructedVoxelStructure::assp_ray_budget_no_history_bias_ = k_default_srvs_param.assp_ray_budget_no_history_bias;
    float ScreenReconstructedVoxelStructure::assp_ray_budget_scale_ = k_default_srvs_param.assp_ray_budget_scale;
    int ScreenReconstructedVoxelStructure::assp_debug_freeze_frame_random_enable_ = k_default_srvs_param.assp_debug_freeze_frame_random_enable;
    int ScreenReconstructedVoxelStructure::dbg_assp_leaf_border_enable_ = k_default_srvs_param.assp_debug_leaf_border_enable;
    int ScreenReconstructedVoxelStructure::dbg_fsp_lighting_interpolation_enable_ = k_default_srvs_param.fsp_lighting_interpolation_enable;
    int ScreenReconstructedVoxelStructure::dbg_fsp_spawn_far_cell_enable_ = k_default_srvs_param.fsp_spawn_far_cell_enable;
    int ScreenReconstructedVoxelStructure::dbg_fsp_lighting_stochastic_sampling_enable_ = k_default_srvs_param.fsp_lighting_stochastic_sampling_enable;
    int ScreenReconstructedVoxelStructure::dbg_fsp_probe_pool_size_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_fsp_free_probe_count_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_fsp_allocated_probe_count_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_fsp_active_probe_count_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_fsp_visible_surface_cell_count_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_assp_total_ray_count_ = 0;
    int ScreenReconstructedVoxelStructure::dbg_assp_probe_count_ = 0;

    void ScreenReconstructedVoxelStructure::DrawDebugMenu(bool* p_enable_injection, bool* p_enable_rejection)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Srvs"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);

            // 右クリックで個別リセット. BeginPopupContextItem は直前のウィジェットを対象とする.
            ImGui::Checkbox("Bbv Injection", p_enable_injection);
            ImGui::Checkbox("Bbv Rejection", p_enable_rejection);

            
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Screen Space Probe"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                
                {
                    bool v = (0 != dbg_ss_probe_temporal_reprojection_enable_);
                    if (ImGui::Checkbox("TemporalReprojection", &v))
                        dbg_ss_probe_temporal_reprojection_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_temporal_reprojection_enable_ = k_default_srvs_param.ss_probe_temporal_reprojection_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_ss_probe_spatial_filter_enable_);
                    if (ImGui::Checkbox("SpatialFilter", &v))
                        dbg_ss_probe_spatial_filter_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_spatial_filter_enable_ = 1;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_ss_probe_ray_guiding_enable_);
                    if (ImGui::Checkbox("RayGuiding", &v))
                        dbg_ss_probe_ray_guiding_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_ray_guiding_enable_ = k_default_srvs_param.ss_probe_ray_guiding_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_ss_probe_side_cache_enable_);
                    if (ImGui::Checkbox("SideCache", &v))
                        dbg_ss_probe_side_cache_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_side_cache_enable_ = k_default_srvs_param.ss_probe_side_cache_enable;
                        ImGui::EndPopup();
                    }
                }

                ImGui::SliderFloat("Probe Relocation Probability", &dbg_ss_probe_preupdate_relocation_probability_, 0.0f, 1.0f, "%.4f");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        dbg_ss_probe_preupdate_relocation_probability_ = k_default_srvs_param.ss_probe_preupdate_relocation_probability;
                    ImGui::EndPopup();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::CollapsingHeader("Temporal Filter"))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);
                    NGL_IMGUI_SCOPED_ID("TemporalFilter");
                    

                    ImGui::SliderFloat("Normal Cos Threshold", &dbg_ss_probe_temporal_filter_normal_cos_threshold_, -1.0f, 1.0f, "%.4f");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_temporal_filter_normal_cos_threshold_ = k_default_srvs_param.ss_probe_temporal_filter_normal_cos_threshold;
                        ImGui::EndPopup();
                    }
                    ImGui::SliderFloat("Plane Distance Threshold", &dbg_ss_probe_temporal_filter_plane_dist_threshold_, 0.0f, 5.0f, "%.4f");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_temporal_filter_plane_dist_threshold_ = k_default_srvs_param.ss_probe_temporal_filter_plane_dist_threshold;
                        ImGui::EndPopup();
                    }
                }
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::CollapsingHeader("Spatial Filter"))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);
                    NGL_IMGUI_SCOPED_ID("SpatialFilter");

                    ImGui::SliderFloat("Normal Cos Threshold", &dbg_ss_probe_spatial_filter_normal_cos_threshold_, -1.0f, 1.0f, "%.4f");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_spatial_filter_normal_cos_threshold_ = k_default_srvs_param.ss_probe_spatial_filter_normal_cos_threshold;
                        ImGui::EndPopup();
                    }
                    ImGui::SliderFloat("Depth Exp Scale", &dbg_ss_probe_spatial_filter_depth_exp_scale_, 0.0f, 500.0f, "%.4f");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_spatial_filter_depth_exp_scale_ = k_default_srvs_param.ss_probe_spatial_filter_depth_exp_scale;
                        ImGui::EndPopup();
                    }
                }
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::CollapsingHeader("Side Cache"))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);
                    NGL_IMGUI_SCOPED_ID("SideCache");
                    
                    ImGui::SliderFloat("Plane Distance Threshold", &dbg_ss_probe_side_cache_plane_dist_threshold_, 0.0f, 5.0f, "%.4f");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_ss_probe_side_cache_plane_dist_threshold_ = k_default_srvs_param.ss_probe_side_cache_plane_dist_threshold;
                        ImGui::EndPopup();
                    }
                }

            }

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Adaptive Screen Space Probe"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                NGL_IMGUI_SCOPED_ID("ASSP");

                {
                    bool v = (0 != assp_spatial_filter_enable_);
                    if (ImGui::Checkbox("SpatialFilter", &v))
                        assp_spatial_filter_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_spatial_filter_enable_ = k_default_srvs_param.assp_spatial_filter_enable;
                        ImGui::EndPopup();
                    }
                }
                ImGui::SliderFloat("Spatial Filter Normal Cos Threshold", &assp_spatial_filter_normal_cos_threshold_, -1.0f, 1.0f, "%.4f");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_spatial_filter_normal_cos_threshold_ = k_default_srvs_param.assp_spatial_filter_normal_cos_threshold;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Spatial Filter Depth Exp Scale", &assp_spatial_filter_depth_exp_scale_, 0.0f, 500.0f, "%.4f");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_spatial_filter_depth_exp_scale_ = k_default_srvs_param.assp_spatial_filter_depth_exp_scale;
                    ImGui::EndPopup();
                }
                {
                    bool v = (0 != assp_temporal_reprojection_enable_);
                    if (ImGui::Checkbox("TemporalReprojection", &v))
                        assp_temporal_reprojection_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_temporal_reprojection_enable_ = k_default_srvs_param.assp_temporal_reprojection_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != assp_ray_guiding_enable_);
                    if (ImGui::Checkbox("RayGuiding", &v))
                        assp_ray_guiding_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_ray_guiding_enable_ = k_default_srvs_param.assp_ray_guiding_enable;
                        ImGui::EndPopup();
                    }
                }
                ImGui::SeparatorText("Ray Budget");
                auto show_ray_budget_tooltip = [](const char* text)
                {
                    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    {
                        ImGui::SetTooltip("%s", text);
                    }
                };
                constexpr int k_assp_ray_budget_ui_max_rays = 31; // packed local ray index is 5-bit.
                ImGui::SliderInt("Min Rays", &assp_ray_budget_min_rays_, 1, k_assp_ray_budget_ui_max_rays);
                show_ray_budget_tooltip("Per-probe ray count lower bound. Final ray count is clamped into [Min Rays, Max Rays].");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_min_rays_ = k_default_srvs_param.assp_ray_budget_min_rays;
                    ImGui::EndPopup();
                }
                ImGui::SliderInt("Max Rays", &assp_ray_budget_max_rays_, 1, k_assp_ray_budget_ui_max_rays);
                show_ray_budget_tooltip("Per-probe ray count upper bound. Values >16 are allowed, while total frame rays remain capped to (probe_count * 16).");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_max_rays_ = k_default_srvs_param.assp_ray_budget_max_rays;
                    ImGui::EndPopup();
                }
                assp_ray_budget_min_rays_ = std::clamp(assp_ray_budget_min_rays_, 1, k_assp_ray_budget_ui_max_rays);
                assp_ray_budget_max_rays_ = std::clamp(assp_ray_budget_max_rays_, 1, k_assp_ray_budget_ui_max_rays);
                if (assp_ray_budget_min_rays_ > assp_ray_budget_max_rays_)
                {
                    assp_ray_budget_max_rays_ = assp_ray_budget_min_rays_;
                }
                ImGui::SliderFloat("Budget Variance Weight", &assp_ray_budget_variance_weight_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Weight of history variance signal. Higher value allocates more rays to temporally unstable probes.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_variance_weight_ = k_default_srvs_param.assp_ray_budget_variance_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Normal Delta Weight", &assp_ray_budget_normal_delta_weight_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Weight of normal change between current tile and best previous tile.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_normal_delta_weight_ = k_default_srvs_param.assp_ray_budget_normal_delta_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Depth Delta Weight", &assp_ray_budget_depth_delta_weight_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Weight of depth change between current tile and best previous tile.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_depth_delta_weight_ = k_default_srvs_param.assp_ray_budget_depth_delta_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget No History Bias", &assp_ray_budget_no_history_bias_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Additional score when no valid temporal history exists. Raises rays for newly observed probes.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_no_history_bias_ = k_default_srvs_param.assp_ray_budget_no_history_bias;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Scale", &assp_ray_budget_scale_, 0.0f, 32.0f, "%.4f");
                show_ray_budget_tooltip("Pre-scale for variance signal before weighting. Higher values make ray distribution react faster.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_scale_ = k_default_srvs_param.assp_ray_budget_scale;
                    ImGui::EndPopup();
                }
                {
                    ImGui::Text("Total Rays (prev frame): %d", dbg_assp_total_ray_count_);
                    const float rays_per_probe = (dbg_assp_probe_count_ > 0)
                        ? (static_cast<float>(dbg_assp_total_ray_count_) / static_cast<float>(dbg_assp_probe_count_))
                        : 0.0f;
                    ImGui::Text("Rays / Probe (prev frame): %.3f (%d probes)", rays_per_probe, dbg_assp_probe_count_);
                    ImGui::TextDisabled("Value is GPU readback from the previous frame.");
                }
                {
                    bool v = (0 != assp_debug_freeze_frame_random_enable_);
                    if (ImGui::Checkbox("Freeze Frame Random", &v))
                        assp_debug_freeze_frame_random_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_debug_freeze_frame_random_enable_ = k_default_srvs_param.assp_debug_freeze_frame_random_enable;
                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Voxel Debug"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                // カテゴリ選択ラジオボタン.
                if (ImGui::RadioButton("Off", dbg_view_category_ == -1)) { dbg_view_category_ = -1; }
                ImGui::SameLine();
                if (ImGui::RadioButton("BBV", dbg_view_category_ == 0)) { dbg_view_category_ = 0; }
                ImGui::SameLine();
                if (ImGui::RadioButton("FSP", dbg_view_category_ == 1)) { dbg_view_category_ = 1; }
                ImGui::SameLine();
                if (ImGui::RadioButton("SSP", dbg_view_category_ == 2)) { dbg_view_category_ = 2; }
                ImGui::SameLine();
                if (ImGui::RadioButton("ASSP", dbg_view_category_ == 3)) { dbg_view_category_ = 3; }

                // カテゴリ別サブモードスライダ.
                if (0 <= dbg_view_category_)
                {
                    const int k_sub_mode_max[] = { 14, 1, 11, 17 };
                    auto get_sub_mode_description = [](int category, int sub_mode) -> const char*
                    {
                        switch(category)
                        {
                        case 0: // BBV
                            switch(sub_mode)
                            {
                            case 0: return "HiBrick trace: voxel ID color";
                            case 1: return "Non-HiBrick trace: fine voxel color";
                            case 2: return "HiBrick trace: brick ID color";
                            case 3: return "HiBrick trace: hit normal";
                            case 4: return "HiBrick trace: hit depth";
                            case 5: return "HiBrick trace: brick occupancy";
                            case 6: return "HiBrick occupancy test trace";
                            case 7: return "Top-down occupancy X-ray";
                            case 8: return "HiBrick empty-skip count";
                            case 9: return "HiBrick occupied-descend count";
                            case 10: return "Brick coarse-check count";
                            case 11: return "Fine voxel/bitmask check count";
                            case 12: return "HiBrick skip efficiency";
                            case 13: return "Cone transmittance approximation";
                            case 14: return "Resolved brick radiance";
                            default: return "Unknown";
                            }
                        case 1: // FSP
                            switch(sub_mode)
                            {
                            case 0: return "FSP Octahedral atlas RGBA";
                            case 1: return "FSP packed SH RGBA";
                            default: return "Unknown";
                            }
                        case 2: // SSP
                            switch(sub_mode)
                            {
                            case 0: return "Probe atlas RGB + sky visibility A";
                            case 1: return "Sky visibility only";
                            case 2: return "Probe normal";
                            case 3: return "Probe placement in tile";
                            case 4: return "Sky visibility SH coefficients";
                            case 5: return "Sky visibility SH sample";
                            case 6: return "Radiance SH coeff 0";
                            case 7: return "Radiance SH coeff 1";
                            case 8: return "Radiance SH coeff 2";
                            case 9: return "Radiance SH coeff 3";
                            case 10: return "Temporal reprojection success";
                            case 11: return "Side cache state";
                            default: return "Unknown";
                            }
                        case 3: // ASSP
                            switch(sub_mode)
                            {
                            case 0: return "Lod0 depth/error/split overview";
                            case 1: return "Lod0 representative depth";
                            case 2: return "Lod0 plane error";
                            case 3: return "Lod0 split score";
                            case 4: return "Lod0 representative normal";
                            case 5: return "Selected LOD color";
                            case 6: return "Leaf noise pattern";
                            case 7: return "Leaf border visualization";
                            case 8: return "Representative lit color";
                            case 9: return "ASSP probe atlas raw";
                            case 10: return "Representative probe sample";
                            case 11: return "ASSP packed SH raw";
                            case 12: return "ASSP SH sample";
                            case 13: return "Filtered variance mean";
                            case 14: return "Filtered variance";
                            case 15: return "Raw variance mean";
                            case 16: return "Raw variance";
                            case 17: return "Per-probe ray count";
                            default: return "Unknown";
                            }
                        default:
                            return "Unknown";
                        }
                    };
                    const int sub_max = k_sub_mode_max[dbg_view_category_];
                    // カテゴリ切替時にクランプ.
                    if (dbg_view_sub_mode_ > sub_max) dbg_view_sub_mode_ = sub_max;
                    if (dbg_view_sub_mode_ < 0) dbg_view_sub_mode_ = 0;
                    ImGui::SliderInt("Sub Mode", &dbg_view_sub_mode_, 0, sub_max);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_view_sub_mode_ = 0;
                        ImGui::EndPopup();
                    }
                    ImGui::TextDisabled("Sub Mode %d: %s", dbg_view_sub_mode_, get_sub_mode_description(dbg_view_category_, dbg_view_sub_mode_));
                }

                if (3 == dbg_view_category_)
                {
                    bool v = (0 != dbg_assp_leaf_border_enable_);
                    if (ImGui::Checkbox("ASSP Leaf Border", &v))
                        dbg_assp_leaf_border_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_assp_leaf_border_enable_ = k_default_srvs_param.assp_debug_leaf_border_enable;
                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Frustum Space Probe"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                {
                    bool v = (0 != dbg_fsp_lighting_interpolation_enable_);
                    if (ImGui::Checkbox("Lighting Interpolation", &v))
                        dbg_fsp_lighting_interpolation_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_fsp_lighting_interpolation_enable_ = k_default_srvs_param.fsp_lighting_interpolation_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_fsp_lighting_stochastic_sampling_enable_);
                    if (ImGui::Checkbox("Stochastic Sampling", &v))
                        dbg_fsp_lighting_stochastic_sampling_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_fsp_lighting_stochastic_sampling_enable_ = k_default_srvs_param.fsp_lighting_stochastic_sampling_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_fsp_spawn_far_cell_enable_);
                    if (ImGui::Checkbox("Spawn Far Cell", &v))
                        dbg_fsp_spawn_far_cell_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_fsp_spawn_far_cell_enable_ = k_default_srvs_param.fsp_spawn_far_cell_enable;
                        ImGui::EndPopup();
                    }
                }
            }

            if (ImGui::CollapsingHeader("Probe Debug"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);

                    ImGui::SliderFloat("Probe Scale", &dbg_probe_scale_, 0.01f, 10.0f);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_probe_scale_ = 1.0f;
                        ImGui::EndPopup();
                    }

                    ImGui::SliderFloat("Probe Near Geometry Scale", &dbg_probe_near_geom_scale_, 0.01f, 10.0f);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_probe_near_geom_scale_ = k_default_srvs_param.debug_probe_near_geom_scale;
                        ImGui::EndPopup();
                    }
                }

                if (ImGui::CollapsingHeader("Frustum Surface Probe", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);

                    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        NGL_IMGUI_SCOPED_INDENT(10.0f);
                        ImGui::Text("Cascade Count: %d", dbg_fsp_cascade_count_);
                        ImGui::Text("Probe Pool Size: %d", dbg_fsp_probe_pool_size_);
                        ImGui::Text("Allocated Probes: %d", dbg_fsp_allocated_probe_count_);
                        ImGui::Text("Free Probes: %d", dbg_fsp_free_probe_count_);
                        ImGui::Text("Active Probes: %d", dbg_fsp_active_probe_count_);
                        ImGui::Text("Visible Surface Cells: %d", dbg_fsp_visible_surface_cell_count_);
                        ImGui::TextDisabled("Stats are GPU readback values from the previous frame.");
                    }

                    if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        NGL_IMGUI_SCOPED_INDENT(10.0f);
                        ImGui::SliderInt("Fsp Probe Mode", &dbg_fsp_probe_debug_mode_, -1, 8);
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Reset to Default"))
                                dbg_fsp_probe_debug_mode_ = k_default_srvs_param.debug_fsp_probe_mode;
                            ImGui::EndPopup();
                        }
                        const int fsp_debug_cascade_max = std::max(-1, dbg_fsp_cascade_count_ - 1);
                        ImGui::SliderInt("Fsp Debug Cascade", &dbg_fsp_probe_debug_cascade_, -1, fsp_debug_cascade_max);
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Reset to Default"))
                                dbg_fsp_probe_debug_cascade_ = -1;
                            ImGui::EndPopup();
                        }
                        ImGui::TextDisabled("-1 = all cascades");
                    }
                }

                if (ImGui::CollapsingHeader("Bitmask Brick Voxel", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);
                    ImGui::SliderInt("Bbv Probe Mode", &dbg_bbv_probe_debug_mode_, -1, 10);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_bbv_probe_debug_mode_ = k_default_srvs_param.debug_bbv_probe_mode;
                        ImGui::EndPopup();
                    }
                }
            }
        }
    }

    using SrvsShaderBindName = ngl::text::HashText<128>;
    constexpr SrvsShaderBindName k_shader_bind_name_fsp_atlas_srv = "FspProbeAtlasTex";
    constexpr SrvsShaderBindName k_shader_bind_name_fsp_atlas_uav = "RWFspProbeAtlasTex";
    constexpr SrvsShaderBindName k_shader_bind_name_fsp_packed_sh_srv = "FspProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_fsp_packed_sh_uav = "RWFspProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_srv = "ScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_history_srv = "ScreenSpaceProbeHistoryTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_uav = "RWScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_tile_info_srv = "ScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_history_tile_info_srv = "ScreenSpaceProbeHistoryTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_tile_info_uav = "RWScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_best_prev_tile_srv = "ScreenSpaceProbeBestPrevTileTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_best_prev_tile_uav = "RWScreenSpaceProbeBestPrevTileTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_filtered_uav = "RWScreenSpaceProbeFilteredTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_packed_sh_srv = "ScreenSpaceProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_packed_sh_uav = "RWScreenSpaceProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_srv = "ScreenSpaceProbeSideCacheTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_uav = "RWScreenSpaceProbeSideCacheTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_meta_srv = "ScreenSpaceProbeSideCacheMetaTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_meta_uav = "RWScreenSpaceProbeSideCacheMetaTex";
    constexpr SrvsShaderBindName k_shader_bind_name_ssprobe_side_cache_lock_uav = "RWScreenSpaceProbeSideCacheLockTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_srv = "AdaptiveScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_history_srv = "AdaptiveScreenSpaceProbeHistoryTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_uav = "RWAdaptiveScreenSpaceProbeTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_tile_info_srv = "AdaptiveScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_history_tile_info_srv = "AdaptiveScreenSpaceProbeHistoryTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_tile_info_uav = "RWAdaptiveScreenSpaceProbeTileInfoTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_best_prev_tile_srv = "AdaptiveScreenSpaceProbeBestPrevTileTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_best_prev_tile_uav = "RWAdaptiveScreenSpaceProbeBestPrevTileTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_variance_srv = "AdaptiveScreenSpaceProbeVarianceTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_history_variance_srv = "AdaptiveScreenSpaceProbeHistoryVarianceTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_variance_uav = "RWAdaptiveScreenSpaceProbeVarianceTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_filtered_uav = "RWAdaptiveScreenSpaceProbeFilteredTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_packed_sh_srv = "AdaptiveScreenSpaceProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_asspprobe_packed_sh_uav = "RWAdaptiveScreenSpaceProbePackedSHTex";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_buffer_srv = "AsspBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_buffer_uav = "RWAsspBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_indirect_arg_uav = "RWAsspProbeIndirectArg";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_trace_indirect_arg_uav = "RWAsspProbeTraceIndirectArg";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_total_ray_count_srv = "AsspProbeTotalRayCountBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_total_ray_count_uav = "RWAsspProbeTotalRayCountBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_meta_srv = "AsspProbeRayMetaBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_meta_uav = "RWAsspProbeRayMetaBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_query_srv = "AsspProbeRayQueryBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_query_uav = "RWAsspProbeRayQueryBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_result_srv = "AsspProbeRayResultBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_assp_probe_ray_result_uav = "RWAsspProbeRayResultBuffer";
    constexpr SrvsShaderBindName k_shader_bind_name_main_lit_color_srv = "TexMainLitColor";

    struct AsspBufferLayout
    {
        u32 total_word_count = 0;
    };

    AsspBufferLayout BuildAsspBufferLayout(u32 screen_width, u32 screen_height)
    {
        AsspBufferLayout layout = {};
        layout.total_word_count = AsspTotalWordCount(screen_width, screen_height);
        return layout;
    }

    ngl::rhi::ConstantBufferPooledHandle AllocSrvsParamCbh(
        ngl::rhi::GraphicsCommandListDep* p_command_list,
        const SrvsParam& param)
    {
        auto cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SrvsParam));
        auto* p_mapped = cbh->buffer.MapAs<SrvsParam>();
        std::memcpy(p_mapped, &param, sizeof(SrvsParam));
        cbh->buffer.Unmap();
        return cbh;
    }
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

    bool BitmaskBrickVoxelGi::ResizeScreenProbeResources(ngl::rhi::DeviceDep* p_device, const math::Vec2i& render_resolution)
    {
        const int ss_probe_base_resolution_x = std::max(render_resolution.x, 1);
        const int ss_probe_base_resolution_y = std::max(render_resolution.y, 1);

        for(auto& tex : ss_probe_tex_) { tex = {}; }
        for(auto& tex : ss_probe_tile_info_tex_) { tex = {}; }
        ss_probe_packed_sh_tex_ = {};
        ss_probe_best_prev_tile_tex_ = {};
        ss_probe_side_cache_tex_ = {};
        ss_probe_side_cache_meta_tex_ = {};
        ss_probe_side_cache_lock_tex_ = {};

        for(auto& tex : assp_probe_tex_) { tex = {}; }
        for(auto& tex : assp_probe_tile_info_tex_) { tex = {}; }
        assp_probe_packed_sh_tex_ = {};
        assp_probe_best_prev_tile_tex_ = {};
        assp_buffer_ = {};
        assp_probe_indirect_arg_ = {};
        assp_probe_trace_indirect_arg_ = {};
        assp_probe_total_ray_count_buffer_ = {};
        assp_probe_ray_meta_buffer_ = {};
        assp_probe_ray_query_buffer_ = {};
        assp_probe_ray_result_buffer_ = {};
        assp_probe_total_ray_count_readback_buffer_ = {};

        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = ss_probe_base_resolution_x;
            desc.height = ss_probe_base_resolution_y;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_tex_[i].Initialize(p_device, desc, (0 == i)? "Srvs_SsProbeTexA" : "Srvs_SsProbeTexB"))
                return false;
        }
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_tile_info_tex_[i].Initialize(p_device, desc, (0 == i)? "Srvs_SsProbeTileInfoTexA" : "Srvs_SsProbeTileInfoTexB"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  ((ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE) * 2;
            desc.height = ((ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE) * 2;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_packed_sh_tex_.Initialize(p_device, desc, "Srvs_SsProbePackedShTex"))
                return false;
        }
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

            if(!ss_probe_side_cache_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheTex"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32G32B32A32_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_side_cache_meta_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheMetaTex"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32_UINT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_side_cache_lock_tex_.Initialize(p_device, desc, "Srvs_SsProbeSideCacheLockTex"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  (ss_probe_base_resolution_x + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (ss_probe_base_resolution_y + SCREEN_SPACE_PROBE_INFO_DOWNSCALE -1) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32_UINT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!ss_probe_best_prev_tile_tex_.Initialize(p_device, desc, "Srvs_SsProbeBestPrevTileTex"))
                return false;
        }

        const int assp_probe_base_resolution_x = ss_probe_base_resolution_x;
        const int assp_probe_base_resolution_y = ss_probe_base_resolution_y;
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = assp_probe_base_resolution_x;
            desc.height = assp_probe_base_resolution_y;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!assp_probe_tex_[i].Initialize(p_device, desc, (0 == i) ? "Srvs_AsspProbeTexA" : "Srvs_AsspProbeTexB"))
                return false;
        }
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = (assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!assp_probe_variance_tex_[i].Initialize(p_device, desc, (0 == i) ? "Srvs_AsspProbeVarianceTexA" : "Srvs_AsspProbeVarianceTexB"))
                return false;
        }
        for(int i = 0; i < 2; ++i)
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = (assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!assp_probe_tile_info_tex_[i].Initialize(p_device, desc, (0 == i) ? "Srvs_AsspProbeTileInfoTexA" : "Srvs_AsspProbeTileInfoTexB"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = ((assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE) * 2;
            desc.height = ((assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE) * 2;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!assp_probe_packed_sh_tex_.Initialize(p_device, desc, "Srvs_AsspProbePackedShTex"))
                return false;
        }
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width = (assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.height = (assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32_UINT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!assp_probe_best_prev_tile_tex_.Initialize(p_device, desc, "Srvs_AsspProbeBestPrevTileTex"))
                return false;
        }
        {
            const AsspBufferLayout assp_layout = BuildAsspBufferLayout(assp_probe_base_resolution_x, assp_probe_base_resolution_y);
            if(!assp_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = assp_layout.total_word_count,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspBuffer"))
            {
                return false;
            }
        }
        {
            if(!assp_probe_indirect_arg_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 3,
                    .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeIndirectArg"))
            {
                return false;
            }
        }
        {
            if(!assp_probe_trace_indirect_arg_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 3,
                    .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeTraceIndirectArg"))
            {
                return false;
            }
        }
        {
            if(!assp_probe_total_ray_count_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 1,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeTotalRayCount"))
            {
                return false;
            }
            if(!InitializeReadbackBuffer(
                p_device,
                assp_probe_total_ray_count_readback_buffer_,
                assp_probe_total_ray_count_buffer_.buffer->GetDesc(),
                "Srvs_AsspProbeTotalRayCountReadback"))
            {
                return false;
            }
        }
        {
            const u32 assp_probe_tile_count =
                static_cast<u32>((assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE) *
                static_cast<u32>((assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
            if(!assp_probe_ray_meta_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = assp_probe_tile_count,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeRayMetaBuffer"))
            {
                return false;
            }
        }
        {
            const u32 assp_probe_tile_count =
                static_cast<u32>((assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE) *
                static_cast<u32>((assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
            constexpr u32 k_assp_max_ray_per_probe = ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
            const u32 element_count = assp_probe_tile_count * k_assp_max_ray_per_probe;
            if(!assp_probe_ray_query_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = element_count,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeRayQueryBuffer"))
            {
                return false;
            }
        }
        {
            const u32 assp_probe_tile_count =
                static_cast<u32>((assp_probe_base_resolution_x + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE) *
                static_cast<u32>((assp_probe_base_resolution_y + ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
            constexpr u32 k_assp_ray_result_stride = 5u;
            constexpr u32 k_assp_max_ray_per_probe = ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
            const u32 element_count = assp_probe_tile_count * k_assp_max_ray_per_probe * k_assp_ray_result_stride;
            if(!assp_probe_ray_result_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = element_count,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "Srvs_AsspProbeRayResultBuffer"))
            {
                return false;
            }
        }

        return true;
    }

    // 初期化
    bool BitmaskBrickVoxelGi::Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg)
    {
        bbv_grid_updater_.Initialize(init_arg.voxel_resolution, init_arg.voxel_size);

        fsp_cascade_count_ = std::clamp<u32>(init_arg.probe_cascade_count, 1u, k_fsp_max_cascade_count);
        fsp_grid_updaters_.resize(fsp_cascade_count_);
        fsp_cascade_cell_offset_array_.resize(fsp_cascade_count_);
        fsp_total_cell_count_ = 0;
        {
            float cascade_cell_size = init_arg.probe_cell_size;
            for(u32 cascade_index = 0; cascade_index < fsp_cascade_count_; ++cascade_index)
            {
                fsp_grid_updaters_[cascade_index].Initialize(init_arg.probe_resolution, cascade_cell_size);
                fsp_cascade_cell_offset_array_[cascade_index] = fsp_total_cell_count_;
                fsp_total_cell_count_ += fsp_grid_updaters_[cascade_index].Get().total_count;
                cascade_cell_size *= 2.0f;
            }
        }


        const auto bbv_grid_resolution = bbv_grid_updater_.Get().resolution;
        const u32 voxel_count = bbv_grid_resolution.x * bbv_grid_resolution.y * bbv_grid_resolution.z;
        // BBV本体バッファは shader 側と同じく
        //   [bitmask region][brick data region][hibrick data region]
        // の順で単一の R32_UINT バッファへ確保する。
        const auto hibrick_grid_resolution = ngl::math::Vec3u(
            (bbv_grid_resolution.x + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution,
            (bbv_grid_resolution.y + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution,
            (bbv_grid_resolution.z + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution);
        const u32 hibrick_count = hibrick_grid_resolution.x * hibrick_grid_resolution.y * hibrick_grid_resolution.z;
        // bitmask は Brick ごとの固定長、brick/hibrick data はそれぞれ別領域の固定長配列として積み上げる。
        const u32 bbv_buffer_element_count =
            voxel_count * k_bbv_per_voxel_bitmask_u32_count +
            voxel_count * k_bbv_brick_data_u32_count +
            hibrick_count * k_bbv_hibrick_data_u32_count;
        // サーフェイスVoxelのリスト. スクリーン上でサーフェイスとして充填された要素を詰め込む. Bbvの充填とは別で, 後処理でサーフェイスVoxelを処理するためのリスト.
        bbv_fine_update_voxel_count_max_= std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);

        // 中空Voxelのクリアキューサイズ. スクリーン上で中空判定された要素を詰め込む.
        bbv_hollow_voxel_list_count_max_= 1024*2;


        fsp_visible_surface_buffer_size_ = k_fsp_probe_surface_cell_count_max;

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
            pso_bbv_clear_  = CreateComputePSO("srvs/bbv/bbv_clear_voxel_cs.hlsl");
            pso_bbv_begin_update_ = CreateComputePSO("srvs/bbv/bbv_begin_update_cs.hlsl");
            pso_bbv_begin_view_update_ = CreateComputePSO("srvs/bbv/bbv_begin_view_update_cs.hlsl");
            pso_bbv_removal_list_build_ = CreateComputePSO("srvs/bbv/bbv_removal_list_build_cs.hlsl");
            pso_bbv_removal_apply_ = CreateComputePSO("srvs/bbv/bbv_removal_apply_cs.hlsl");
            pso_bbv_injection_apply_     = CreateComputePSO("srvs/bbv/bbv_injection_apply_cs.hlsl");
            pso_bbv_radiance_injection_apply_ = CreateComputePSO("srvs/bbv/bbv_radiance_injection_apply_cs.hlsl");
            pso_bbv_radiance_resolve_ = CreateComputePSO("srvs/bbv/bbv_radiance_resolve_cs.hlsl");
            pso_bbv_brick_count_aggregate_ = CreateComputePSO("srvs/bbv/bbv_brick_count_aggregate_cs.hlsl");
            pso_bbv_hibrick_count_aggregate_ = CreateComputePSO("srvs/bbv/bbv_hibrick_count_aggregate_cs.hlsl");
            pso_bbv_generate_visible_voxel_indirect_arg_ = CreateComputePSO("srvs/bbv/bbv_generate_visible_surface_list_indirect_arg_cs.hlsl");
            pso_bbv_removal_indirect_arg_build_ = CreateComputePSO("srvs/bbv/bbv_removal_indirect_arg_build_cs.hlsl");
            pso_bbv_element_update_ = CreateComputePSO("srvs/bbv/bbv_element_update_cs.hlsl");
            pso_bbv_visible_surface_element_update_ = CreateComputePSO("srvs/bbv/bbv_visible_surface_element_update_cs.hlsl");

            pso_fsp_clear_ = CreateComputePSO("srvs/fsp/fsp_clear_voxel_cs.hlsl");
            pso_fsp_begin_update_ = CreateComputePSO("srvs/fsp/fsp_begin_update_cs.hlsl");
            pso_fsp_visible_surface_proc_ = CreateComputePSO("srvs/fsp/fsp_screen_space_pass_cs.hlsl");
            pso_fsp_generate_indirect_arg_ = CreateComputePSO("srvs/fsp/fsp_generate_indirect_arg_cs.hlsl");
            pso_fsp_pre_update_ = CreateComputePSO("srvs/fsp/fsp_pre_update_cs.hlsl");
            pso_fsp_update_ = CreateComputePSO("srvs/fsp/fsp_update_cs.hlsl");
            pso_fsp_sh_update_ = CreateComputePSO("srvs/fsp/fsp_probe_sh_update_cs.hlsl");

            pso_ss_probe_clear_ = CreateComputePSO("srvs/ssp/ss_probe_clear_cs.hlsl");
            pso_ss_probe_preupdate_ = CreateComputePSO("srvs/ssp/ss_probe_preupdate_cs.hlsl");
            pso_ss_probe_update_ = CreateComputePSO("srvs/ssp/ss_probe_update_cs.hlsl");
            pso_ss_probe_spatial_filter_ = CreateComputePSO("srvs/ssp/ss_probe_spatial_filter_cs.hlsl");
            pso_ss_probe_sh_update_ = CreateComputePSO("srvs/ssp/ss_probe_sh_update_cs.hlsl");
            pso_assp_probe_clear_ = CreateComputePSO("srvs/assp/assp_probe_clear_cs.hlsl");
            pso_assp_probe_preupdate_ = CreateComputePSO("srvs/assp/assp_probe_preupdate_cs.hlsl");
            pso_assp_probe_generate_indirect_arg_ = CreateComputePSO("srvs/assp/assp_probe_generate_indirect_arg_cs.hlsl");
            pso_assp_probe_build_ray_meta_ = CreateComputePSO("srvs/assp/assp_probe_build_ray_meta_cs.hlsl");
            pso_assp_probe_finalize_ray_query_ = CreateComputePSO("srvs/assp/assp_probe_finalize_ray_query_cs.hlsl");
            pso_assp_probe_trace_ = CreateComputePSO("srvs/assp/assp_probe_trace_cs.hlsl");
            pso_assp_probe_update_ = CreateComputePSO("srvs/assp/assp_probe_update_cs.hlsl");
            pso_assp_probe_spatial_filter_ = CreateComputePSO("srvs/assp/assp_probe_spatial_filter_cs.hlsl");
            pso_assp_probe_variance_ = CreateComputePSO("srvs/assp/assp_probe_variance_cs.hlsl");
            pso_assp_probe_sh_update_ = CreateComputePSO("srvs/assp/assp_probe_sh_update_cs.hlsl");
            pso_assp_depth_analysis_ = CreateComputePSO("srvs/assp/assp_depth_analysis_cs.hlsl");

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
                    pso_fsp_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
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
                    pso_fsp_debug_probe_ = pso_cache->GetOrCreate(p_device, gpso_desc);
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
            bbv_radiance_accum_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = voxel_count * k_bbv_radiance_accum_component_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "Srvs_BbvRadianceAccumBuffer");
        }
        {
            bbv_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = bbv_buffer_element_count,

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
            // 1F更新可能プローブ数分の k_fsp_probe_octmap_width*k_fsp_probe_octmap_width テクセル分バッファ.
            bbv_fine_update_voxel_probe_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(float),
                                               .element_count     = bbv_fine_update_voxel_count_max_ * (k_fsp_probe_octmap_width*k_fsp_probe_octmap_width),

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
            // fsp_buffer_初期化.
            fsp_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(FspProbeData),
                                               .element_count     = fsp_total_cell_count_,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default}
                                            ,   "Srvs_FspBuffer");
        }
        {
            // V1 FSP lifecycle: cell -> probe index only.
            fsp_cell_probe_index_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = fsp_total_cell_count_,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "Srvs_FspCellProbeIndexBuffer");
        }
        {
            fsp_probe_pool_size_ = k_fsp_probe_pool_size;
            const auto NextPow2 = [](u32 value) -> u32
            {
                u32 result = 1;
                while (result < value)
                {
                    result <<= 1;
                }
                return result;
            };
            fsp_probe_atlas_tile_width_ = NextPow2(static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(fsp_probe_pool_size_)))));
            fsp_probe_atlas_tile_height_ = (fsp_probe_pool_size_ + fsp_probe_atlas_tile_width_ - 1) / fsp_probe_atlas_tile_width_;
            fsp_probe_pool_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                                .element_byte_size = sizeof(FspProbePoolData),
                                               .element_count     = fsp_probe_pool_size_,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default}
                                            ,  "Srvs_FspProbePoolBuffer");
        }
        {
            fsp_probe_free_stack_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = fsp_probe_pool_size_ + 1, // 0番はstack counter/head用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "Srvs_FspProbeFreeStack");
        }
        for (u32 active_list_index = 0; active_list_index < 2; ++active_list_index)
        {
            const std::string resource_name = std::string("Srvs_FspActiveProbeList") + std::to_string(active_list_index);
            fsp_active_probe_list_[active_list_index].InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = fsp_probe_pool_size_ + 1, // 0番はcounter.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  resource_name.c_str());
        }
        {
            fsp_visible_surface_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = fsp_visible_surface_buffer_size_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_FspVisibleSurfaceList");
            InitializeReadbackBuffer(p_device, fsp_visible_surface_list_readback_buffer_, fsp_visible_surface_list_.buffer->GetDesc(), "Srvs_FspVisibleSurfaceListReadback");
        }
        {
            fsp_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                                .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                                .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "Srvs_FspIndirectArg");
        }
        InitializeReadbackBuffer(p_device, fsp_probe_free_stack_readback_buffer_, fsp_probe_free_stack_buffer_.buffer->GetDesc(), "Srvs_FspProbeFreeStackReadback");
        InitializeReadbackBuffer(p_device, fsp_active_probe_list_readback_buffer_, fsp_active_probe_list_[0].buffer->GetDesc(), "Srvs_FspActiveProbeListReadback");

        // FSP プローブアトラス.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  fsp_probe_atlas_tile_width_ * k_fsp_probe_octmap_width;
            desc.height = fsp_probe_atlas_tile_height_ * k_fsp_probe_octmap_width;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;// Enhanced Barrier移行時はCommonのみ許可.

            fsp_probe_atlas_tex_.Initialize(p_device, desc, "Srvs_FspProbeAtlasTex");
        }
        // Frustum Surface Probe Packed SH テクスチャ.
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  fsp_probe_atlas_tile_width_ * 2;
            desc.height = fsp_probe_atlas_tile_height_ * 2;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            fsp_probe_packed_sh_tex_.Initialize(p_device, desc, "Srvs_FspProbePackedShTex");
        }

        if(!ResizeScreenProbeResources(p_device, math::Vec2i(1920, 1080)))
        {
            return false;
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

        const math::Vec2i desired_probe_resolution(std::max(render_resolution.x, 1), std::max(render_resolution.y, 1));
        const bool needs_screen_probe_resize =
            (nullptr == ss_probe_tex_[0].texture.Get()) ||
            (static_cast<int>(ss_probe_tex_[0].texture->GetWidth()) != desired_probe_resolution.x) ||
            (static_cast<int>(ss_probe_tex_[0].texture->GetHeight()) != desired_probe_resolution.y);
        if(needs_screen_probe_resize)
        {
            const bool resize_success = ResizeScreenProbeResources(p_command_list->GetDevice(), desired_probe_resolution);
            assert(resize_success);
            if(!resize_success)
            {
                return;
            }

            is_first_dispatch_ = true;
            ss_probe_prev_frame_tex_index_ = 0;
            ss_probe_curr_frame_tex_index_ = 0;
            ss_probe_latest_filtered_frame_tex_index_ = 0;
            ss_probe_tile_info_prev_frame_tex_index_ = 0;
            ss_probe_tile_info_curr_frame_tex_index_ = 0;
            assp_prev_frame_tex_index_ = 0;
            assp_curr_frame_tex_index_ = 0;
            assp_latest_filtered_frame_tex_index_ = 0;
            assp_variance_prev_frame_tex_index_ = 0;
            assp_variance_curr_frame_tex_index_ = 0;
            assp_tile_info_prev_frame_tex_index_ = 0;
            assp_tile_info_curr_frame_tex_index_ = 0;
        }

        const bool is_first_dispatch = is_first_dispatch_;
        is_first_dispatch_           = false;
        ++frame_count_;

        ss_probe_prev_frame_tex_index_ = ss_probe_curr_frame_tex_index_;
        ss_probe_curr_frame_tex_index_ = 1 - ss_probe_prev_frame_tex_index_;
        ss_probe_latest_filtered_frame_tex_index_ = ss_probe_prev_frame_tex_index_;

        ss_probe_tile_info_prev_frame_tex_index_ = ss_probe_tile_info_curr_frame_tex_index_;
        ss_probe_tile_info_curr_frame_tex_index_ = 1 - ss_probe_tile_info_prev_frame_tex_index_;

        assp_prev_frame_tex_index_ = assp_curr_frame_tex_index_;
        assp_curr_frame_tex_index_ = 1 - assp_prev_frame_tex_index_;
        assp_latest_filtered_frame_tex_index_ = assp_prev_frame_tex_index_;
        assp_variance_prev_frame_tex_index_ = assp_variance_curr_frame_tex_index_;
        assp_variance_curr_frame_tex_index_ = 1 - assp_variance_prev_frame_tex_index_;

        assp_tile_info_prev_frame_tex_index_ = assp_tile_info_curr_frame_tex_index_;
        assp_tile_info_curr_frame_tex_index_ = 1 - assp_tile_info_prev_frame_tex_index_;

        // 重視位置を若干補正.
        #if 0
            const math::Vec3 modified_important_point = important_point_ + important_dir_ * 5.0f;
        #else
            const math::Vec3 modified_important_point = important_point_;
        #endif

        
        #if 1
        {
            bbv_grid_updater_.UpdateGrid(modified_important_point);
            for(auto& fsp_grid_updater : fsp_grid_updaters_)
            {
                fsp_grid_updater.UpdateGrid(modified_important_point);
            }
        }
        #else
            // FIXME. デバッグ. gridの移動を止めて外部からレイトレースをした場合のデバッグ等.
        #endif

        const math::Vec2i hw_depth_size = render_resolution;

        cbh_dispatch_ = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SrvsParam));
        {
            // メンバデフォルト値で初期化し、ランタイム可変値のみ上書き.
            SrvsParam param{};

            //Bbv
            {
                param.bbv.grid_resolution = bbv_grid_updater_.Get().resolution.Cast<int>();
                param.bbv.grid_min_pos     = bbv_grid_updater_.Get().min_pos;
                param.bbv.grid_min_voxel_coord = math::Vec3::Floor(bbv_grid_updater_.Get().min_pos * (1.0f / bbv_grid_updater_.Get().cell_size)).Cast<int>();

                param.bbv.grid_toroidal_offset =  bbv_grid_updater_.Get().toroidal_offset;
                param.bbv.grid_toroidal_offset_prev =  bbv_grid_updater_.Get().toroidal_offset_prev;

                param.bbv.grid_move_cell_delta = bbv_grid_updater_.Get().min_pos_delta_cell;

                param.bbv.flatten_2d_width = bbv_grid_updater_.Get().flatten_2d_width;

                param.bbv.cell_size       = bbv_grid_updater_.Get().cell_size;
                param.bbv.cell_size_inv    = 1.0f / bbv_grid_updater_.Get().cell_size;

                param.bbv_indirect_cs_thread_group_size = math::Vec3i(pso_bbv_visible_surface_element_update_->GetThreadGroupSizeX(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeY(), pso_bbv_visible_surface_element_update_->GetThreadGroupSizeZ());
                param.bbv_visible_voxel_buffer_size = bbv_fine_update_voxel_count_max_;
                param.bbv_hollow_voxel_buffer_size = bbv_hollow_voxel_list_count_max_;
            }
            // Fsp
            {
                param.fsp_indirect_cs_thread_group_size = math::Vec3i(pso_fsp_pre_update_->GetThreadGroupSizeX(), pso_fsp_pre_update_->GetThreadGroupSizeY(), pso_fsp_pre_update_->GetThreadGroupSizeZ());
                param.fsp_visible_voxel_buffer_size = fsp_visible_surface_buffer_size_;
                param.fsp_probe_pool_size = static_cast<int>(fsp_probe_pool_size_);
                param.fsp_active_probe_buffer_size = static_cast<int>(fsp_probe_pool_size_);
                param.fsp_lighting_interpolation_enable = ScreenReconstructedVoxelStructure::dbg_fsp_lighting_interpolation_enable_;
                param.fsp_spawn_far_cell_enable = ScreenReconstructedVoxelStructure::dbg_fsp_spawn_far_cell_enable_;
                param.fsp_lighting_stochastic_sampling_enable = ScreenReconstructedVoxelStructure::dbg_fsp_lighting_stochastic_sampling_enable_;
                param.fsp_cascade_count = static_cast<int>(fsp_cascade_count_);
                param.fsp_total_cell_count = static_cast<int>(fsp_total_cell_count_);
                param.fsp_probe_atlas_tile_width = static_cast<int>(fsp_probe_atlas_tile_width_);
                param.fsp_probe_atlas_tile_height = static_cast<int>(fsp_probe_atlas_tile_height_);

                for(u32 cascade_index = 0; cascade_index < fsp_cascade_count_; ++cascade_index)
                {
                    const auto& cascade_grid = fsp_grid_updaters_[cascade_index].Get();
                    auto& cascade_param = param.fsp_cascade[cascade_index];
                    cascade_param.grid.grid_resolution = cascade_grid.resolution.Cast<int>();
                    cascade_param.grid.grid_min_pos = cascade_grid.min_pos;
                    cascade_param.grid.grid_min_voxel_coord = math::Vec3::Floor(cascade_grid.min_pos * (1.0f / cascade_grid.cell_size)).Cast<int>();
                    cascade_param.grid.grid_toroidal_offset = cascade_grid.toroidal_offset;
                    cascade_param.grid.grid_toroidal_offset_prev = cascade_grid.toroidal_offset_prev;
                    cascade_param.grid.grid_move_cell_delta = cascade_grid.min_pos_delta_cell;
                    cascade_param.grid.flatten_2d_width = cascade_grid.flatten_2d_width;
                    cascade_param.grid.cell_size = cascade_grid.cell_size;
                    cascade_param.grid.cell_size_inv = 1.0f / cascade_grid.cell_size;
                    cascade_param.cell_offset = fsp_cascade_cell_offset_array_[cascade_index];
                    cascade_param.cell_count = cascade_grid.total_count;
                }
            }

            param.tex_main_view_depth_size = hw_depth_size;
            param.frame_count = frame_count_;
            const AsspBufferLayout assp_layout = BuildAsspBufferLayout(static_cast<u32>(hw_depth_size.x), static_cast<u32>(hw_depth_size.y));
            param.assp_words_per_node = static_cast<int>(k_assp_words_per_node);
            param.assp_total_word_count = static_cast<int>(assp_layout.total_word_count);
            param.assp_tile_size = static_cast<int>(k_assp_tile_size);

            // dbg_系: ランタイム変更可能なパラメータ.
            param.ss_probe_spatial_filter_normal_cos_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_normal_cos_threshold_;
            param.ss_probe_spatial_filter_depth_exp_scale = ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_depth_exp_scale_;
            param.ss_probe_temporal_reprojection_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_reprojection_enable_;
            param.ss_probe_ray_guiding_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_ray_guiding_enable_;
            param.ss_probe_side_cache_enable = ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_enable_;
            param.ss_probe_preupdate_relocation_probability = ScreenReconstructedVoxelStructure::dbg_ss_probe_preupdate_relocation_probability_;
            param.ss_probe_temporal_filter_normal_cos_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_normal_cos_threshold_;
            param.ss_probe_temporal_filter_plane_dist_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_temporal_filter_plane_dist_threshold_;
            param.ss_probe_side_cache_plane_dist_threshold = ScreenReconstructedVoxelStructure::dbg_ss_probe_side_cache_plane_dist_threshold_;

            param.main_light_dir_ws = main_view_info.main_light_dir_ws;

            param.debug_view_category = ScreenReconstructedVoxelStructure::dbg_view_category_;
            param.debug_view_sub_mode = ScreenReconstructedVoxelStructure::dbg_view_sub_mode_;
            param.debug_bbv_probe_mode = ScreenReconstructedVoxelStructure::dbg_bbv_probe_debug_mode_;
            param.debug_fsp_probe_mode = ScreenReconstructedVoxelStructure::dbg_fsp_probe_debug_mode_;
            param.debug_fsp_probe_cascade = ScreenReconstructedVoxelStructure::dbg_fsp_probe_debug_cascade_;

            param.debug_probe_radius = ScreenReconstructedVoxelStructure::dbg_probe_scale_ * 0.5f * bbv_grid_updater_.Get().cell_size / k_bbv_per_voxel_resolution;
            param.debug_probe_near_geom_scale = ScreenReconstructedVoxelStructure::dbg_probe_near_geom_scale_;
            param.assp_spatial_filter_enable = ScreenReconstructedVoxelStructure::assp_spatial_filter_enable_;
            param.assp_spatial_filter_normal_cos_threshold = ScreenReconstructedVoxelStructure::assp_spatial_filter_normal_cos_threshold_;
            param.assp_spatial_filter_depth_exp_scale = ScreenReconstructedVoxelStructure::assp_spatial_filter_depth_exp_scale_;
            param.assp_temporal_reprojection_enable = ScreenReconstructedVoxelStructure::assp_temporal_reprojection_enable_;
            param.assp_ray_guiding_enable = ScreenReconstructedVoxelStructure::assp_ray_guiding_enable_;
            param.assp_ray_budget_min_rays = ScreenReconstructedVoxelStructure::assp_ray_budget_min_rays_;
            param.assp_ray_budget_max_rays = ScreenReconstructedVoxelStructure::assp_ray_budget_max_rays_;
            param.assp_ray_budget_variance_weight = ScreenReconstructedVoxelStructure::assp_ray_budget_variance_weight_;
            param.assp_ray_budget_normal_delta_weight = ScreenReconstructedVoxelStructure::assp_ray_budget_normal_delta_weight_;
            param.assp_ray_budget_depth_delta_weight = ScreenReconstructedVoxelStructure::assp_ray_budget_depth_delta_weight_;
            param.assp_ray_budget_no_history_bias = ScreenReconstructedVoxelStructure::assp_ray_budget_no_history_bias_;
            param.assp_ray_budget_scale = ScreenReconstructedVoxelStructure::assp_ray_budget_scale_;
            param.assp_debug_freeze_frame_random_enable = ScreenReconstructedVoxelStructure::assp_debug_freeze_frame_random_enable_;
            param.assp_debug_leaf_border_enable = ScreenReconstructedVoxelStructure::dbg_assp_leaf_border_enable_;

            dispatch_param_cache_ = param;
            // ローカル変数からマップ先バッファへコピー.
            auto* p_mapped = cbh_dispatch_->buffer.MapAs<SrvsParam>();
            std::memcpy(p_mapped, &dispatch_param_cache_, sizeof(SrvsParam));
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
                pso_bbv_clear_->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());
                pso_bbv_clear_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_clear_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_clear_.Get(), &desc_set);
                pso_bbv_clear_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

                p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspInitClear");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_clear_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_clear_->SetView(&desc_set, "RWFspProbeBuffer", fsp_buffer_.uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWFspCellProbeIndexBuffer", fsp_cell_probe_index_buffer_.uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWFspProbePoolBuffer", fsp_probe_pool_buffer_.uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWFspProbeFreeStack", fsp_probe_free_stack_buffer_.uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWFspActiveProbeListPrev", fsp_active_probe_list_[0].uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWFspActiveProbeListCurr", fsp_active_probe_list_[1].uav.Get());
                pso_fsp_clear_->SetView(&desc_set, "RWSurfaceProbeCellList", fsp_visible_surface_list_.uav.Get());
                pso_fsp_clear_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_uav.Get(), fsp_probe_atlas_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_clear_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_clear_.Get(), &desc_set);
                pso_fsp_clear_->DispatchHelper(p_command_list, std::max<u32>(fsp_total_cell_count_, fsp_probe_pool_size_ + 1), 1, 1);

                p_command_list->ResourceUavBarrier(fsp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_active_probe_list_[0].buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_active_probe_list_[1].buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_visible_surface_list_.buffer.Get());
                p_command_list->ResourceBarrier(fsp_probe_atlas_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
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
                p_command_list->SetPipelineState(pso_assp_probe_clear_.Get());
                for(int i = 0; i < 2; ++i)
                {
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_assp_probe_clear_->SetView(&desc_set, k_shader_bind_name_asspprobe_uav.Get(), assp_probe_tex_[i].uav.Get());
                    pso_assp_probe_clear_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_uav.Get(), assp_probe_tile_info_tex_[i].uav.Get());
                    p_command_list->SetDescriptorSet(pso_assp_probe_clear_.Get(), &desc_set);
                    pso_assp_probe_clear_->DispatchHelper(p_command_list, assp_probe_tex_[i].texture->GetWidth(), assp_probe_tex_[i].texture->GetHeight(), 1);

                    p_command_list->ResourceBarrier(assp_probe_tex_[i].texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(assp_probe_variance_tex_[i].texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(assp_probe_tile_info_tex_[i].texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                }
                p_command_list->ResourceBarrier(fsp_probe_packed_sh_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(ss_probe_packed_sh_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(assp_probe_packed_sh_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(ss_probe_best_prev_tile_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(assp_probe_best_prev_tile_tex_.texture.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(assp_buffer_.buffer.Get(), rhi::EResourceState::Common, rhi::EResourceState::UnorderedAccess);

            }
        }
        // Bbv Begin Update Pass.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_begin_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());
            pso_bbv_begin_update_->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());
            pso_bbv_begin_update_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_begin_update_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_begin_update_.Get(), &desc_set);
            pso_bbv_begin_update_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
            p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
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

        // BBV count rebuild pass.
        // Injection / Removal は bitmask のみを更新し、Brick / HiBrick count はここで再構築する。
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBrickCountAggregate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_brick_count_aggregate_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_brick_count_aggregate_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_brick_count_aggregate_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_brick_count_aggregate_.Get(), &desc_set);
            pso_bbv_brick_count_aggregate_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
        }
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvHiBrickCountAggregate");

            const auto bbv_grid_resolution = bbv_grid_updater_.Get().resolution;
            // HiBrick は logical 2x2x2 Brick cluster 数で Dispatch する。
            const auto hibrick_grid_resolution = ngl::math::Vec3u(
                (bbv_grid_resolution.x + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution,
                (bbv_grid_resolution.y + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution,
                (bbv_grid_resolution.z + k_bbv_hibrick_brick_resolution - 1) / k_bbv_hibrick_brick_resolution);
            const u32 hibrick_count = hibrick_grid_resolution.x * hibrick_grid_resolution.y * hibrick_grid_resolution.z;

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_hibrick_count_aggregate_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_hibrick_count_aggregate_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_hibrick_count_aggregate_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_hibrick_count_aggregate_.Get(), &desc_set);
            pso_bbv_hibrick_count_aggregate_->DispatchHelper(p_command_list, hibrick_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
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

    void BitmaskBrickVoxelGi::Dispatch_Bbv_RadianceInjection_View(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv,
        const ngl::render::task::RenderPassViewInfo& main_view_info,
        const InjectionSourceDepthBufferViewInfo& view_info)
    {
        if(!view_info.is_enable_radiance_injection_pass || !view_info.hw_depth_srv.IsValid() || !view_info.hw_color_srv.IsValid())
            return;

        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Dispatch_Bbv_RadianceInjection_View");

        auto cbh_injection_view_info = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(BbvSurfaceInjectionViewInfo));
        {
            auto* p = cbh_injection_view_info->buffer.MapAs<BbvSurfaceInjectionViewInfo>();
            p->cb_view_mtx = view_info.view_mat;
            p->cb_proj_mtx = view_info.proj_mat;
            p->cb_view_inv_mtx = ngl::math::Mat34::Inverse(view_info.view_mat);
            p->cb_proj_inv_mtx = ngl::math::Mat44::Inverse(view_info.proj_mat);
            p->cb_ndc_z_to_view_z_coef = CalcViewDepthReconstructCoefFromProjectionMatrix(view_info.proj_mat);
            p->cb_view_depth_buffer_offset_size = math::Vec4i(
                view_info.atlas_offset.x,
                view_info.atlas_offset.y,
                view_info.atlas_resolution.x,
                view_info.atlas_resolution.y);
            cbh_injection_view_info->buffer.Unmap();
        }

        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRadianceInjection");
            const auto injection_dispatch_resolution = CalcBbvRadianceInjectionDispatchResolution(math::Vec2u(
                static_cast<u32>(view_info.atlas_resolution.x),
                static_cast<u32>(view_info.atlas_resolution.y)));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_radiance_injection_apply_->SetView(&desc_set, "TexHardwareDepth", view_info.hw_depth_srv.Get());
            pso_bbv_radiance_injection_apply_->SetView(&desc_set, "TexInputRadiance", view_info.hw_color_srv.Get());
            pso_bbv_radiance_injection_apply_->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
            pso_bbv_radiance_injection_apply_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_radiance_injection_apply_->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_radiance_injection_apply_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_radiance_injection_apply_.Get(), &desc_set);
            // 1F に各 2x2 screen tile group から 1 tile だけ更新する前提なので、dispatch も group 数に合わせる。
            pso_bbv_radiance_injection_apply_->DispatchHelper(p_command_list, injection_dispatch_resolution.x, injection_dispatch_resolution.y, 1);

            p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
        }
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRadianceResolve");
            const auto resolve_dispatch_count = CalcBbvRadianceResolveDispatchCount(bbv_grid_updater_.Get().resolution);

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_radiance_resolve_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_radiance_resolve_->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());
            pso_bbv_radiance_resolve_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_radiance_resolve_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_radiance_resolve_.Get(), &desc_set);
            // 1F に各 2x2x2 group から 1 Brick だけ更新する前提なので、dispatch 数も group 数に合わせる。
            pso_bbv_radiance_resolve_->DispatchHelper(p_command_list, resolve_dispatch_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
            p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
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
    
    void BitmaskBrickVoxelGi::Dispatch_SsProbe(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_SsProbe");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));
        const ngl::u32 ss_probe_history_index = ss_probe_prev_frame_tex_index_;
        const ngl::u32 ss_probe_update_write_index = ss_probe_curr_frame_tex_index_;
        const ngl::u32 ss_probe_tile_info_history_index = ss_probe_tile_info_prev_frame_tex_index_;
        const ngl::u32 ss_probe_tile_info_curr_index = ss_probe_tile_info_curr_frame_tex_index_;
        const bool is_ss_probe_spatial_filter_enable = (0 != ScreenReconstructedVoxelStructure::dbg_ss_probe_spatial_filter_enable_);

        ngl::u32 ss_probe_sh_input_index = ss_probe_update_write_index;

        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbePreUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_ss_probe_preupdate_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_ss_probe_preupdate_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_ss_probe_preupdate_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].uav.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_ssprobe_history_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_history_index].srv.Get());
                pso_ss_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_ssprobe_best_prev_tile_uav.Get(), ss_probe_best_prev_tile_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_preupdate_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_preupdate_.Get(), &desc_set);
                // PreUpdate は 1 スレッドグループ(3x3) = 1 ProbeTile.
                p_command_list->Dispatch(ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture->GetWidth(),
                    ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture->GetHeight(), 1);

                p_command_list->ResourceUavBarrier(ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].texture.Get());
                p_command_list->ResourceUavBarrier(ss_probe_best_prev_tile_tex_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "ScreenSpaceProbeUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                //pso_ss_probe_update_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_ss_probe_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_ss_probe_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_uav.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_index].uav.Get());
                pso_ss_probe_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_best_prev_tile_srv.Get(), ss_probe_best_prev_tile_tex_.srv.Get());
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
                pso_ss_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_ssprobe_packed_sh_uav.Get(), ss_probe_packed_sh_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_ss_probe_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_ss_probe_sh_update_.Get(), &desc_set);
                pso_ss_probe_sh_update_->DispatchHelper(
                    p_command_list,
                    ss_probe_packed_sh_tex_.texture->GetWidth() / 2,
                    ss_probe_packed_sh_tex_.texture->GetHeight() / 2,
                    1);

                p_command_list->ResourceUavBarrier(ss_probe_packed_sh_tex_.texture.Get());
            }
        }
    }

    void BitmaskBrickVoxelGi::Dispatch_AsspProbe(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_AsspProbe");

        const ngl::u32 assp_probe_update_write_index = assp_curr_frame_tex_index_;
        const ngl::u32 assp_probe_tile_info_curr_index = assp_tile_info_curr_frame_tex_index_;
        const ngl::u32 assp_probe_history_index = assp_prev_frame_tex_index_;
        const ngl::u32 assp_probe_tile_info_history_index = assp_tile_info_prev_frame_tex_index_;
        const ngl::u32 assp_probe_variance_write_index = assp_variance_curr_frame_tex_index_;
        const ngl::u32 assp_probe_variance_history_index = assp_variance_prev_frame_tex_index_;
        const bool is_assp_spatial_filter_enable = (0 != ScreenReconstructedVoxelStructure::assp_spatial_filter_enable_);

        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbePreUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_preupdate_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_assp_probe_preupdate_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_assp_probe_preupdate_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_history_index].srv.Get());
                pso_assp_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_uav.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].uav.Get());
                pso_assp_probe_preupdate_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_uav.Get(), assp_probe_best_prev_tile_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_preupdate_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_preupdate_.Get(), &desc_set);
                pso_assp_probe_preupdate_->DispatchHelper(
                    p_command_list,
                    assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture->GetWidth(),
                    assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture->GetHeight(),
                    1);

                p_command_list->ResourceUavBarrier(assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture.Get());
                p_command_list->ResourceUavBarrier(assp_probe_best_prev_tile_tex_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeIndirectArg");

                assp_probe_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_generate_indirect_arg_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_generate_indirect_arg_->SetView(&desc_set, k_shader_bind_name_assp_probe_indirect_arg_uav.Get(), assp_probe_indirect_arg_.uav.Get());
                pso_assp_probe_generate_indirect_arg_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_uav.Get(), assp_probe_total_ray_count_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_generate_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_generate_indirect_arg_.Get(), &desc_set);
                pso_assp_probe_generate_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                p_command_list->ResourceUavBarrier(assp_probe_total_ray_count_buffer_.buffer.Get());
                assp_probe_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeBuildRayMeta");

                assp_probe_ray_meta_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                assp_probe_ray_query_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_history_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_variance_srv.Get(), assp_probe_variance_tex_[assp_probe_variance_history_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_uav.Get(), assp_probe_ray_meta_buffer_.uav.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_query_uav.Get(), assp_probe_ray_query_buffer_.uav.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_uav.Get(), assp_probe_total_ray_count_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_build_ray_meta_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_build_ray_meta_.Get(), &desc_set);
                p_command_list->DispatchIndirect(assp_probe_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(assp_probe_ray_meta_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(assp_probe_ray_query_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(assp_probe_total_ray_count_buffer_.buffer.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeFinalizeRayQuery");

                assp_probe_trace_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_finalize_ray_query_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_srv.Get(), assp_probe_total_ray_count_buffer_.srv.Get());
                pso_assp_probe_finalize_ray_query_->SetView(&desc_set, k_shader_bind_name_assp_probe_trace_indirect_arg_uav.Get(), assp_probe_trace_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_finalize_ray_query_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_finalize_ray_query_.Get(), &desc_set);
                pso_assp_probe_finalize_ray_query_->DispatchHelper(p_command_list, 1, 1, 1);

                assp_probe_ray_query_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
                assp_probe_trace_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeTrace");

                assp_probe_ray_result_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_trace_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_assp_probe_trace_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_trace_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_srv.Get(), assp_probe_tex_[assp_probe_history_index].srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_srv.Get(), assp_probe_total_ray_count_buffer_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_srv.Get(), assp_probe_ray_meta_buffer_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_query_srv.Get(), assp_probe_ray_query_buffer_.srv.Get());
                pso_assp_probe_trace_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_result_uav.Get(), assp_probe_ray_result_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_trace_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_trace_.Get(), &desc_set);
                p_command_list->DispatchIndirect(assp_probe_trace_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(assp_probe_ray_result_buffer_.buffer.Get());
                assp_probe_ray_result_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeUpdateResolve");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_assp_probe_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_srv.Get(), assp_probe_tex_[assp_probe_history_index].srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_srv.Get(), assp_probe_ray_meta_buffer_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_result_srv.Get(), assp_probe_ray_result_buffer_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_uav.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].uav.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_uav.Get(), assp_probe_tex_[assp_probe_update_write_index].uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_update_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_update_.Get(), &desc_set);

                p_command_list->DispatchIndirect(assp_probe_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(assp_probe_tex_[assp_probe_update_write_index].texture.Get());
                p_command_list->ResourceUavBarrier(assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture.Get());
                assp_latest_filtered_frame_tex_index_ = assp_probe_update_write_index;
            }
            if(is_assp_spatial_filter_enable)
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeSpatialFilter");

                const ngl::u32 assp_probe_filter_input_index = assp_probe_update_write_index;
                const ngl::u32 assp_probe_filter_output_index = 1 - assp_probe_filter_input_index;

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_spatial_filter_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_probe_filter_input_index].srv.Get());
                pso_assp_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_spatial_filter_->SetView(&desc_set, k_shader_bind_name_asspprobe_filtered_uav.Get(), assp_probe_tex_[assp_probe_filter_output_index].uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_spatial_filter_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_spatial_filter_.Get(), &desc_set);
                pso_assp_probe_spatial_filter_->DispatchHelper(
                    p_command_list,
                    assp_probe_tex_[assp_probe_filter_output_index].texture->GetWidth(),
                    assp_probe_tex_[assp_probe_filter_output_index].texture->GetHeight(),
                    1);

                p_command_list->ResourceUavBarrier(assp_probe_tex_[assp_probe_filter_output_index].texture.Get());

                assp_latest_filtered_frame_tex_index_ = assp_probe_filter_output_index;
                assp_curr_frame_tex_index_ = assp_latest_filtered_frame_tex_index_;
                assp_prev_frame_tex_index_ = 1 - assp_curr_frame_tex_index_;
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeVariance");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_variance_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                // LOD split/merge は spatial filter 後ではなく、生の update 出力に対する分散を見て判定する。
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_probe_update_write_index].srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_variance_srv.Get(), assp_probe_variance_tex_[assp_probe_variance_history_index].srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_variance_uav.Get(), assp_probe_variance_tex_[assp_probe_variance_write_index].uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_variance_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_variance_.Get(), &desc_set);
                p_command_list->DispatchIndirect(assp_probe_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(assp_probe_variance_tex_[assp_probe_variance_write_index].texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeShUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_sh_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv.Get());
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_packed_sh_uav.Get(), assp_probe_packed_sh_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_sh_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(assp_probe_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(assp_probe_packed_sh_tex_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AsspReadbackCopy");

                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::CopySrc);
                p_command_list->CopyResource(assp_probe_total_ray_count_readback_buffer_.Get(), assp_probe_total_ray_count_buffer_.buffer.Get());
                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            }
        }
    }

    void BitmaskBrickVoxelGi::Dispatch_AsspHierarchy(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_AsspHierarchy");

        const u32 assp_lod0_width = AsspLodWidth(static_cast<u32>(dispatch_param_cache_.tex_main_view_depth_size.x), 0u);
        const u32 assp_lod0_height = AsspLodHeight(static_cast<u32>(dispatch_param_cache_.tex_main_view_depth_size.y), 0u);

        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AsspLod0Build");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_assp_depth_analysis_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_assp_depth_analysis_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            pso_assp_depth_analysis_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_assp_depth_analysis_->SetView(&desc_set, k_shader_bind_name_assp_buffer_uav.Get(), assp_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_assp_depth_analysis_.Get());
            p_command_list->SetDescriptorSet(pso_assp_depth_analysis_.Get(), &desc_set);
            pso_assp_depth_analysis_->DispatchHelper(p_command_list, assp_lod0_width, assp_lod0_height, 1);

            p_command_list->ResourceUavBarrier(assp_buffer_.buffer.Get());
        }
        // 4x4固定所有に移行したため、LOD1 build は不要。
    }

    void BitmaskBrickVoxelGi::Dispatch_Fsp(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Fsp");

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));
        const u32 fsp_active_probe_curr_list_index = frame_count_ & 1u;
        const u32 fsp_active_probe_prev_list_index = 1u - fsp_active_probe_curr_list_index;
        auto& fsp_active_probe_curr_list = fsp_active_probe_list_[fsp_active_probe_curr_list_index];
        auto& fsp_active_probe_prev_list = fsp_active_probe_list_[fsp_active_probe_prev_list_index];

        // FSP.
        {
            // Fsp Prev Active IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspGeneratePrevActiveIndirectArg");

                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "ProbeIndexList", fsp_active_probe_prev_list.srv.Get());
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "RWFspIndirectArg", fsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_generate_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_generate_indirect_arg_.Get(), &desc_set);
                pso_fsp_generate_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Fsp Begin Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspBeginUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_fsp_begin_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_begin_update_->SetView(&desc_set, "RWFspProbeBuffer", fsp_buffer_.uav.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "RWFspCellProbeIndexBuffer", fsp_cell_probe_index_buffer_.uav.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "RWFspProbePoolBuffer", fsp_probe_pool_buffer_.uav.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "RWFspProbeFreeStack", fsp_probe_free_stack_buffer_.uav.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "FspActiveProbeListPrev", fsp_active_probe_prev_list.srv.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "RWFspActiveProbeListCurr", fsp_active_probe_curr_list.uav.Get());
                pso_fsp_begin_update_->SetView(&desc_set, "RWSurfaceProbeCellList", fsp_visible_surface_list_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_begin_update_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_begin_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(fsp_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(fsp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_active_probe_curr_list.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_visible_surface_list_.buffer.Get());
            }
            
            // Fsp Visible Surface Processing Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspVisibleSurfaceProcessing");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_visible_surface_proc_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_fsp_visible_surface_proc_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_fsp_visible_surface_proc_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_visible_surface_proc_->SetView(&desc_set, "RWFspProbeBuffer", fsp_buffer_.uav.Get());
                pso_fsp_visible_surface_proc_->SetView(&desc_set, "RWSurfaceProbeCellList", fsp_visible_surface_list_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_visible_surface_proc_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_visible_surface_proc_.Get(), &desc_set);
                pso_fsp_visible_surface_proc_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);  // Screen処理でDispatch.

                p_command_list->ResourceUavBarrier(fsp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_visible_surface_list_.buffer.Get());
            }
            // Fsp IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspGenerateIndirectArg");
                 
                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "ProbeIndexList", fsp_visible_surface_list_.srv.Get());
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "RWFspIndirectArg", fsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_generate_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_generate_indirect_arg_.Get(), &desc_set);
                pso_fsp_generate_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Fsp PreUpdate Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspPreUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_pre_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_fsp_pre_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_pre_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                pso_fsp_pre_update_->SetView(&desc_set, "SurfaceProbeCellList", fsp_visible_surface_list_.srv.Get());
                pso_fsp_pre_update_->SetView(&desc_set, "RWFspCellProbeIndexBuffer", fsp_cell_probe_index_buffer_.uav.Get());
                pso_fsp_pre_update_->SetView(&desc_set, "RWFspProbePoolBuffer", fsp_probe_pool_buffer_.uav.Get());
                pso_fsp_pre_update_->SetView(&desc_set, "RWFspProbeFreeStack", fsp_probe_free_stack_buffer_.uav.Get());
                pso_fsp_pre_update_->SetView(&desc_set, "RWFspActiveProbeListCurr", fsp_active_probe_curr_list.uav.Get());
                pso_fsp_pre_update_->SetView(&desc_set, "RWFspProbeBuffer", fsp_buffer_.uav.Get());
                pso_fsp_pre_update_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_uav.Get(), fsp_probe_atlas_tex_.uav.Get());


                p_command_list->SetPipelineState(pso_fsp_pre_update_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_pre_update_.Get(), &desc_set);

                p_command_list->DispatchIndirect(fsp_indirect_arg_.buffer.Get());// 可視SurfaceListDispatch.


                p_command_list->ResourceUavBarrier(fsp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_active_probe_curr_list.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_atlas_tex_.texture.Get());
            }
            // Fsp Current Active IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspGenerateActiveIndirectArg");

                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "ProbeIndexList", fsp_active_probe_curr_list.srv.Get());
                pso_fsp_generate_indirect_arg_->SetView(&desc_set, "RWFspIndirectArg", fsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_generate_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_generate_indirect_arg_.Get(), &desc_set);
                pso_fsp_generate_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                fsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Fsp Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_fsp_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_fsp_update_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());

                pso_fsp_update_->SetView(&desc_set, "FspActiveProbeListCurr", fsp_active_probe_curr_list.srv.Get());
                pso_fsp_update_->SetView(&desc_set, "RWFspCellProbeIndexBuffer", fsp_cell_probe_index_buffer_.uav.Get());
                pso_fsp_update_->SetView(&desc_set, "RWFspProbePoolBuffer", fsp_probe_pool_buffer_.uav.Get());
                pso_fsp_update_->SetView(&desc_set, "RWFspProbeFreeStack", fsp_probe_free_stack_buffer_.uav.Get());
                pso_fsp_update_->SetView(&desc_set, "RWFspProbeBuffer", fsp_buffer_.uav.Get());
                pso_fsp_update_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_uav.Get(), fsp_probe_atlas_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_update_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(fsp_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(fsp_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(fsp_probe_atlas_tex_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspProbeShUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_fsp_sh_update_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
                pso_fsp_sh_update_->SetView(&desc_set, "FspActiveProbeListCurr", fsp_active_probe_curr_list.srv.Get());
                pso_fsp_sh_update_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_srv.Get(), fsp_probe_atlas_tex_.srv.Get());
                pso_fsp_sh_update_->SetView(&desc_set, "FspProbePoolBuffer", fsp_probe_pool_buffer_.srv.Get());
                pso_fsp_sh_update_->SetView(&desc_set, k_shader_bind_name_fsp_packed_sh_uav.Get(), fsp_probe_packed_sh_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_fsp_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_fsp_sh_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(fsp_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(fsp_probe_packed_sh_tex_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspReadbackCopy");

                p_command_list->ResourceBarrier(fsp_visible_surface_list_.buffer.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::CopySrc);
                p_command_list->ResourceBarrier(fsp_probe_free_stack_buffer_.buffer.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::CopySrc);
                p_command_list->ResourceBarrier(fsp_active_probe_curr_list.buffer.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::CopySrc);

                p_command_list->CopyResource(fsp_visible_surface_list_readback_buffer_.Get(), fsp_visible_surface_list_.buffer.Get());
                p_command_list->CopyResource(fsp_probe_free_stack_readback_buffer_.Get(), fsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->CopyResource(fsp_active_probe_list_readback_buffer_.Get(), fsp_active_probe_curr_list.buffer.Get());

                p_command_list->ResourceBarrier(fsp_visible_surface_list_.buffer.Get(), rhi::EResourceState::CopySrc, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(fsp_probe_free_stack_buffer_.buffer.Get(), rhi::EResourceState::CopySrc, rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(fsp_active_probe_curr_list.buffer.Get(), rhi::EResourceState::CopySrc, rhi::EResourceState::UnorderedAccess);
            }
        }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_Debug(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefSrvDep lit_color_srv, rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Srvs_Dispatch_Debug");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        // デバッグ描画準備.
        if(0 <= ScreenReconstructedVoxelStructure::dbg_view_category_)
        {
            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_srv.Get(), fsp_probe_atlas_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_fsp_packed_sh_srv.Get(), fsp_probe_packed_sh_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_srv.Get(), ss_probe_tex_[ss_probe_latest_filtered_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), ss_probe_tile_info_tex_[ss_probe_tile_info_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_packed_sh_srv.Get(), ss_probe_packed_sh_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_srv.Get(), ss_probe_side_cache_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_ssprobe_side_cache_meta_srv.Get(), ss_probe_side_cache_meta_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_variance_srv.Get(), assp_probe_variance_tex_[assp_variance_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_packed_sh_srv.Get(), assp_probe_packed_sh_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_srv.Get(), assp_probe_ray_meta_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_assp_buffer_srv.Get(), assp_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_main_lit_color_srv.Get(), lit_color_srv.Get());
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
        if (0 <= ScreenReconstructedVoxelStructure::dbg_fsp_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "FspProbeDebug");

            p_command_list->SetPipelineState(pso_fsp_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_fsp_debug_probe_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);

            pso_fsp_debug_probe_->SetView(&desc_set, "cb_srvs", &cbh_dispatch_->cbv);
            pso_fsp_debug_probe_->SetView(&desc_set, "FspCellProbeIndexBuffer", fsp_cell_probe_index_buffer_.srv.Get());
            pso_fsp_debug_probe_->SetView(&desc_set, "FspProbePoolBuffer", fsp_probe_pool_buffer_.srv.Get());
            pso_fsp_debug_probe_->SetView(&desc_set, "FspProbeBuffer", fsp_buffer_.srv.Get());
            pso_fsp_debug_probe_->SetView(&desc_set, k_shader_bind_name_fsp_atlas_srv.Get(), fsp_probe_atlas_tex_.srv.Get());
            pso_fsp_debug_probe_->SetView(&desc_set, k_shader_bind_name_fsp_packed_sh_srv.Get(), fsp_probe_packed_sh_tex_.srv.Get());
            pso_fsp_debug_probe_->SetView(&desc_set, "SmpLinearClamp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());


            p_command_list->SetDescriptorSet(pso_fsp_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * fsp_total_cell_count_, 1, 0, 0);
        }

    }


    // ----------------------------------------------------------------

    
    ScreenReconstructedVoxelStructure::~ScreenReconstructedVoxelStructure()
    {
        Finalize();
    }

    // 初期化
    bool ScreenReconstructedVoxelStructure::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u fsp_resolution, float fsp_cell_size, u32 fsp_cascade_count)
    {
        bbvgi_instance_ = new BitmaskBrickVoxelGi();
        BitmaskBrickVoxelGi::InitArg init_arg = {};
        {
            init_arg.voxel_resolution = bbv_resolution;
            init_arg.voxel_size       = bbv_cell_size;

            init_arg.probe_resolution = fsp_resolution;
            init_arg.probe_cell_size  = fsp_cell_size;
            init_arg.probe_cascade_count = fsp_cascade_count;
        }
        if(!bbvgi_instance_->Initialize(p_device, init_arg))
        {
            delete bbvgi_instance_;
            bbvgi_instance_ = nullptr;
            return false;
        }

        is_initialized_ = true;
        dbg_fsp_cascade_count_ = static_cast<int>(std::clamp<u32>(fsp_cascade_count, 1u, k_fsp_max_cascade_count));
        dbg_fsp_probe_debug_cascade_ = std::clamp(dbg_fsp_probe_debug_cascade_, -1, dbg_fsp_cascade_count_ - 1);
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

    void BitmaskBrickVoxelGi::UpdateFspDebugReadback()
    {
        auto read_counter = [](rhi::RefBufferDep buffer) -> int
        {
            if (buffer.Get() == nullptr)
            {
                return 0;
            }
            if (auto* mapped = buffer->MapAs<uint32_t>())
            {
                const int value = static_cast<int>(mapped[0]);
                buffer->Unmap();
                return value;
            }
            return 0;
        };

        ScreenReconstructedVoxelStructure::dbg_fsp_probe_pool_size_ = static_cast<int>(fsp_probe_pool_size_);
        ScreenReconstructedVoxelStructure::dbg_fsp_free_probe_count_ = read_counter(fsp_probe_free_stack_readback_buffer_);
        ScreenReconstructedVoxelStructure::dbg_fsp_active_probe_count_ = read_counter(fsp_active_probe_list_readback_buffer_);
        ScreenReconstructedVoxelStructure::dbg_fsp_visible_surface_cell_count_ = read_counter(fsp_visible_surface_list_readback_buffer_);
        ScreenReconstructedVoxelStructure::dbg_fsp_allocated_probe_count_ =
            std::max(0, ScreenReconstructedVoxelStructure::dbg_fsp_probe_pool_size_ - ScreenReconstructedVoxelStructure::dbg_fsp_free_probe_count_);
    }

    void BitmaskBrickVoxelGi::UpdateAsspDebugReadback()
    {
        if (assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].texture.Get())
        {
            const auto* p_tex = assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].texture.Get();
            ScreenReconstructedVoxelStructure::dbg_assp_probe_count_ =
                static_cast<int>(p_tex->GetWidth() * p_tex->GetHeight());
        }
        else
        {
            ScreenReconstructedVoxelStructure::dbg_assp_probe_count_ = 0;
        }

        if (assp_probe_total_ray_count_readback_buffer_.Get() == nullptr)
        {
            ScreenReconstructedVoxelStructure::dbg_assp_total_ray_count_ = 0;
            return;
        }
        if (auto* mapped = assp_probe_total_ray_count_readback_buffer_->MapAs<uint32_t>())
        {
            ScreenReconstructedVoxelStructure::dbg_assp_total_ray_count_ = static_cast<int>(mapped[0]);
            assp_probe_total_ray_count_readback_buffer_->Unmap();
            return;
        }
        ScreenReconstructedVoxelStructure::dbg_assp_total_ray_count_ = 0;
    }

    void ScreenReconstructedVoxelStructure::DispatchBegin(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, const math::Vec2i& render_resolution)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->UpdateFspDebugReadback();
            bbvgi_instance_->UpdateAsspDebugReadback();
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
    void ScreenReconstructedVoxelStructure::DispatchViewBbvRadianceInjection(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv,
        const ngl::render::task::RenderPassViewInfo& main_view_info,
        const InjectionSourceDepthBufferViewInfo& view_info)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_RadianceInjection_View(p_command_list, scene_cbv, main_view_info, view_info);
        }
    }
    void ScreenReconstructedVoxelStructure::DispatchUpdate(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_Main(p_command_list, scene_cbv);
            bbvgi_instance_->Dispatch_AsspHierarchy(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
            bbvgi_instance_->Dispatch_AsspProbe(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
            bbvgi_instance_->Dispatch_SsProbe(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
            bbvgi_instance_->Dispatch_Fsp(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
        }
    }

    void ScreenReconstructedVoxelStructure::DispatchDebug(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        rhi::RefSrvDep lit_color_srv, rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Debug(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv, lit_color_srv, work_tex, work_uav);
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
        p_pso->SetView(p_desc_set, k_shader_bind_name_fsp_atlas_srv.Get(), bbvgi_instance_->GetFspProbeAtlasTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_fsp_packed_sh_srv.Get(), bbvgi_instance_->GetFspProbePackedShTex().Get());
        p_pso->SetView(p_desc_set, "FspCellProbeIndexBuffer", bbvgi_instance_->GetFspCellProbeIndexBuffer().Get());
        p_pso->SetView(p_desc_set, "FspProbePoolBuffer", bbvgi_instance_->GetFspProbePoolBuffer().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_srv.Get(), bbvgi_instance_->GetSsProbeTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_tile_info_srv.Get(), bbvgi_instance_->GetSsProbeTileInfoTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_ssprobe_packed_sh_srv.Get(), bbvgi_instance_->GetSsProbePackedShTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_assp_buffer_srv.Get(), bbvgi_instance_->GetAsspBuffer().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), bbvgi_instance_->GetAsspProbeTileInfoTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_asspprobe_packed_sh_srv.Get(), bbvgi_instance_->GetAsspProbePackedShTex().Get());
        p_pso->SetView(p_desc_set, "cb_srvs", &bbvgi_instance_->GetDispatchCbh()->cbv);
    }

}  // namespace ngl::render::app

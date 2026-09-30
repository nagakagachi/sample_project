/*
    instant_rdv.cpp
    Instant Raster Derived Voxel Sceneの描画パス実装。
*/

#include "render/app/instant_rdv/instant_rdv.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <limits>
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
    #include "../shader/instant_rdv/instant_rdv_common_header.hlsli"
    #undef NGL_SHADER_CPP_INCLUDE


    static constexpr u32 k_max_update_probe_work_count = 1024;
    static constexpr float k_occupancy_injection_default_fine_cells = 2.0f;
    static constexpr size_t k_bbv_depth_cull_plane_count = 6;

    static std::array<math::Vec4, k_bbv_depth_cull_plane_count> CalcBbvFrustumPlanes(
        const math::Mat34& view_mat,
        const math::Mat44& proj_mat)
    {
        // Native cull uses clip = proj * view * world. These are the same
        // homogeneous clip inequalities used by the existing center test.
        const math::Mat44 view_mat44(
            view_mat.r0,
            view_mat.r1,
            view_mat.r2,
            math::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const math::Mat44 world_to_clip = proj_mat * view_mat44;
        const math::Vec4 clip_x = world_to_clip.r0;
        const math::Vec4 clip_y = world_to_clip.r1;
        const math::Vec4 clip_z = world_to_clip.r2;
        const math::Vec4 clip_w = world_to_clip.r3;

        const bool is_reverse_z = proj_mat.m[2][3] > 0.0f;
        std::array<math::Vec4, k_bbv_depth_cull_plane_count> planes = {
            clip_w + clip_x,
            clip_w - clip_x,
            clip_w + clip_y,
            clip_w - clip_y,
            is_reverse_z ? (clip_w - clip_z) : clip_z,
            is_reverse_z ? clip_z : (clip_w - clip_z)
        };

        for (math::Vec4& plane : planes)
        {
            const float normal_length = math::Vec3(plane.x, plane.y, plane.z).Length();
            if (std::isfinite(normal_length) && normal_length > 1.0e-20f)
            {
                plane = plane / normal_length;
            }
            else
            {
                // Infinite-far projections have no finite far-plane boundary.
                plane = math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        return planes;
    }

    static float CalcOccupancyInjectionWorldOffsetFromFineCells(float injection_fine_cells, float bbv_cell_size)
    {
        return bbv_cell_size * (injection_fine_cells * float(k_bbv_per_voxel_resolution_inv));
    }

    static constexpr u32 k_vsp_probe_pool_size = 1<<13;//10000;
    static constexpr u32 k_vsp_probe_surface_cell_count_max = 1024*4;// index list only; tile-sliceの一時的な候補増加で欠落しない余裕を持つ.
    static constexpr u32 k_vsp_ray_request_oct_cell_bits = 8u;
    static_assert(
        k_vsp_probe_octmap_width * k_vsp_probe_octmap_width <= (1u << k_vsp_ray_request_oct_cell_bits),
        "VSP ray request key reserves only 8 bits for the octahedral-map cell index.");
    static_assert(
        k_vsp_probe_pool_size <= ((1u << (32u - k_vsp_ray_request_oct_cell_bits)) - 1u),
        "VSP probe index does not fit in the packed ray request key.");
    static_assert(
        k_vsp_irradiance_volume_sh_texture_count == 4,
        "VSP IrradianceVolume shaders assume exactly four L1 SH signal textures.");

    static bool ValidateVspInitArg(const BitmaskBrickVoxelGi::InitArg& init_arg)
    {
        const auto Fail = [](const char* message)
        {
            std::cout << "[ERROR] Invalid VSP configuration: " << message << std::endl;
            return false;
        };

        const auto& bbv_resolution = init_arg.voxel_resolution;
        if(bbv_resolution.x == 0u || bbv_resolution.y == 0u || bbv_resolution.z == 0u ||
           bbv_resolution.x > 1024u || bbv_resolution.y > 1024u || bbv_resolution.z > 1024u)
        {
            return Fail("BBV voxel resolution must fit Morton X10Y10Z10 (1..1024 on every axis).");
        }
        if(bbv_resolution.x != bbv_resolution.y || bbv_resolution.x != bbv_resolution.z ||
           (bbv_resolution.x & (bbv_resolution.x - 1u)) != 0u)
        {
            return Fail("BBV Morton buffer indexing requires a cubic power-of-two voxel resolution.");
        }
        if(!std::isfinite(init_arg.voxel_size) || init_arg.voxel_size <= 0.0f)
        {
            return Fail("BBV voxel size must be finite and greater than zero.");
        }
        const uint64_t bbv_voxel_count =
            uint64_t(bbv_resolution.x) * uint64_t(bbv_resolution.y) * uint64_t(bbv_resolution.z);
        if(bbv_voxel_count > uint64_t(std::numeric_limits<u32>::max()))
        {
            return Fail("BBV voxel count overflows u32.");
        }
        const uint64_t bbv_buffer_element_count =
            bbv_voxel_count *
            uint64_t(k_bbv_per_voxel_bitmask_u32_count + k_bbv_brick_data_u32_count);
        if(bbv_buffer_element_count > uint64_t(std::numeric_limits<u32>::max()))
        {
            return Fail("BBV buffer element count overflows u32.");
        }

        const auto& resolution = init_arg.probe_resolution;
        if(resolution.x < 4u || resolution.y < 4u || resolution.z < 4u)
        {
            return Fail("probe resolution must be at least 4 on every axis for analytic cascade selection.");
        }
        if(resolution.x != resolution.y || resolution.x != resolution.z)
        {
            return Fail("IrradianceVolume requires an equal resolution on all three axes.");
        }
        if((resolution.x & (resolution.x - 1u)) != 0u)
        {
            return Fail("IrradianceVolume resolution must be a power of two for bit-mask Toroidal wrapping.");
        }
        if(init_arg.probe_cascade_count == 0u || init_arg.probe_cascade_count > k_vsp_max_cascade_count)
        {
            return Fail("cascade count is outside the shader constant-buffer array range.");
        }
        if(!std::isfinite(init_arg.probe_cell_size) || init_arg.probe_cell_size <= 0.0f)
        {
            return Fail("probe cell size must be finite and greater than zero.");
        }

        const uint64_t cell_count_per_cascade =
            uint64_t(resolution.x) * uint64_t(resolution.y) * uint64_t(resolution.z);
        const uint64_t total_cell_count =
            cell_count_per_cascade * uint64_t(init_arg.probe_cascade_count);
        if(total_cell_count > uint64_t(std::numeric_limits<int>::max()))
        {
            return Fail("total IrradianceVolume cell count does not fit in the shader parameter.");
        }
        const uint64_t padded_texture_width = uint64_t(resolution.x) +
            uint64_t(k_vsp_irradiance_volume_guard_texel_count);
        const uint64_t padded_texture_height = uint64_t(resolution.y) +
            uint64_t(k_vsp_irradiance_volume_guard_texel_count);
        const uint64_t padded_texture_slice_depth = uint64_t(resolution.z) +
            uint64_t(k_vsp_irradiance_volume_guard_texel_count);
        const uint64_t texture_depth = padded_texture_slice_depth *
            uint64_t(init_arg.probe_cascade_count) *
            uint64_t(k_vsp_irradiance_volume_sh_texture_count);
        constexpr uint64_t k_d3d12_texture3d_dimension_limit = 2048;
        if(padded_texture_width > k_d3d12_texture3d_dimension_limit ||
            padded_texture_height > k_d3d12_texture3d_dimension_limit ||
            texture_depth > k_d3d12_texture3d_dimension_limit)
        {
            return Fail("IrradianceVolume 3D texture depth exceeds the D3D12 limit.");
        }

        const float coarsest_cell_size = std::ldexp(
            init_arg.probe_cell_size,
            static_cast<int>(init_arg.probe_cascade_count - 1u));
        if(!std::isfinite(coarsest_cell_size))
        {
            return Fail("coarsest cascade cell size overflows float.");
        }
        return true;
    }

    enum class InstantRdvGiSolutionMode : int
    {
        None = 0,
        Vsp = 2,
        Assp = 3,
    };

    const char* InstantRdvGiSolutionModeName(int mode)
    {
        switch(static_cast<InstantRdvGiSolutionMode>(mode))
        {
        case InstantRdvGiSolutionMode::None: return "None";
        case InstantRdvGiSolutionMode::Vsp: return "VSP";
        case InstantRdvGiSolutionMode::Assp: return "ASSP";
        default: return "Unknown";
        }
    }

    const char* VspProbeDebugModeLabel(int mode)
    {
        switch(mode)
        {
        case -1: return "-1: Disabled";
        case 0: return "0: Seen this frame (green) / stale (yellow)";
        case 1: return "1: Probe index hash color";
        case 2: return "2: Probe age heat (fresh -> old)";
        case 3: return "3: Cascade index hash color";
        case 4: return "4: Oct radiance (tonemapped)";
        case 5: return "5: Oct sky visibility (current dir)";
        case 6: return "6: SH radiance (reconstructed)";
        case 7: return "7: SH sky visibility (reconstructed)";
        case 8: return "8: ActiveProbe BBV occupancy (outside / embedded / free)";
        case 9: return "9: Camera-to-relocated-probe BBV reachability (outside / blocked / reached)";
        default: return "Unknown mode";
        }
    }

    const char* VspProbeDebugModeDetailLabel(int mode)
    {
        switch(mode)
        {
        case 3: return "Age(frames)=frame_count-last_seen, 0->green, 15->yellow, 30+->red";
        default: return "";
        }
    }

    const char* VspIrradianceVolumeDebugModeLabel(int mode)
    {
        switch(mode)
        {
        case -1: return "-1: Disabled";
        case 0: return "0: SH radiance";
        case 1: return "1: SH sky visibility";
        default: return "Unknown mode";
        }
    }

    const char* BbvProbeDebugModeLabel(int mode)
    {
        switch(mode)
        {
        case -1: return "-1: Disabled";
        case 0: return "0: Surface distance grayscale";
        default: return "Other: Default normal color";
        }
    }

    struct InstantRdvGiDispatchEntry
    {
        InstantRdvGiSolutionMode mode;
        void (BitmaskBrickVoxelGi::*dispatch_func)(rhi::GraphicsCommandListDep*,
            rhi::ConstantBufferPooledHandle,
            const ngl::render::task::RenderPassViewInfo&, rhi::RefTextureDep, rhi::RefSrvDep);
    };

    // GIソリューションの実行順序/対応を集中管理する。
    // 追加/削除時はこのテーブルとモード定義の更新に集約する。
    constexpr std::array<InstantRdvGiDispatchEntry, 2> k_instant_rdv_gi_dispatch_entries = {{
        { InstantRdvGiSolutionMode::Assp, &BitmaskBrickVoxelGi::Dispatch_AsspProbe },
        { InstantRdvGiSolutionMode::Vsp,  &BitmaskBrickVoxelGi::Dispatch_Vsp },
    }};
    
    
    static math::Vec2u CalcBbvRadianceInjectionDispatchResolution(const math::Vec2u& src_resolution)
    {
        const math::Vec2u tile_grid_resolution(
            (src_resolution.x +
             k_bbv_radiance_injection_tile_width - 1u) /
                k_bbv_radiance_injection_tile_width,
            (src_resolution.y +
             k_bbv_radiance_injection_tile_width - 1u) /
                k_bbv_radiance_injection_tile_width);
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
    int InstantRasterDerivedVoxelScene::dbg_view_category_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_view_sub_mode_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scale_ =
        k_default_instant_rdv_param.debug_vsp_irradiance_volume_slice_scale;
    int InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scroll_x_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scroll_y_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_signal_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_cascade_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_trilinear_enable_ = 1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_cascade_interpolation_enable_ = 1;
    float InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_irradiance_ev_ = 0.0f;
    math::Vec3u InstantRasterDerivedVoxelScene::dbg_vsp_resolution_ = math::Vec3u(1);
    int InstantRasterDerivedVoxelScene::dbg_bbv_probe_debug_mode_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_bbv_depth_test_enable_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_debug_mode_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_debug_mode_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_depth_test_ = 1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_use_relocated_pos_ = k_default_instant_rdv_param.debug_vsp_probe_use_relocated_pos;
    int InstantRasterDerivedVoxelScene::dbg_vsp_update_ray_jitter_enable_ = k_default_instant_rdv_param.debug_vsp_update_ray_jitter_enable;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_debug_cascade_ = -1;
    int InstantRasterDerivedVoxelScene::dbg_vsp_cascade_count_ = 1;
    float InstantRasterDerivedVoxelScene::dbg_vsp_relocation_offset_scale_for_cascade_cell_size_ = k_default_instant_rdv_param.vsp_relocation_offset_scale_for_cascade_cell_size;
    float InstantRasterDerivedVoxelScene::dbg_probe_scale_ = 1.0f;
    float InstantRasterDerivedVoxelScene::dbg_probe_near_geom_scale_ = 0.2f;
    int InstantRasterDerivedVoxelScene::assp_spatial_filter_enable_ = k_default_instant_rdv_param.assp_spatial_filter_enable;
    float InstantRasterDerivedVoxelScene::assp_spatial_filter_normal_cos_threshold_ = k_default_instant_rdv_param.assp_spatial_filter_normal_cos_threshold;
    float InstantRasterDerivedVoxelScene::assp_spatial_filter_depth_exp_scale_ = k_default_instant_rdv_param.assp_spatial_filter_depth_exp_scale;
    int InstantRasterDerivedVoxelScene::assp_temporal_reprojection_enable_ = k_default_instant_rdv_param.assp_temporal_reprojection_enable;
    int InstantRasterDerivedVoxelScene::assp_ray_guiding_enable_ = k_default_instant_rdv_param.assp_ray_guiding_enable;
    int InstantRasterDerivedVoxelScene::assp_ray_budget_min_rays_ = k_default_instant_rdv_param.assp_ray_budget_min_rays;
    int InstantRasterDerivedVoxelScene::assp_ray_budget_max_rays_ = k_default_instant_rdv_param.assp_ray_budget_max_rays;
    float InstantRasterDerivedVoxelScene::assp_ray_budget_variance_weight_ = k_default_instant_rdv_param.assp_ray_budget_variance_weight;
    float InstantRasterDerivedVoxelScene::assp_ray_budget_normal_delta_weight_ = k_default_instant_rdv_param.assp_ray_budget_normal_delta_weight;
    float InstantRasterDerivedVoxelScene::assp_ray_budget_depth_delta_weight_ = k_default_instant_rdv_param.assp_ray_budget_depth_delta_weight;
    float InstantRasterDerivedVoxelScene::assp_ray_budget_no_history_bias_ = k_default_instant_rdv_param.assp_ray_budget_no_history_bias;
    float InstantRasterDerivedVoxelScene::assp_ray_budget_scale_ = k_default_instant_rdv_param.assp_ray_budget_scale;
    int InstantRasterDerivedVoxelScene::assp_debug_freeze_frame_random_enable_ = k_default_instant_rdv_param.assp_debug_freeze_frame_random_enable;
    int InstantRasterDerivedVoxelScene::dbg_vsp_lighting_interpolation_enable_ = k_default_instant_rdv_param.vsp_lighting_interpolation_enable;
    int InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_propagate_active_probe_weight_enable_ = k_default_instant_rdv_param.vsp_irradiance_volume_propagate_active_probe_weight_enable;
    float InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_propagate_active_probe_weight_scale_ = k_default_instant_rdv_param.vsp_irradiance_volume_propagate_active_probe_weight_scale;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_lifecycle_enable_ = k_default_instant_rdv_param.vsp_probe_lifecycle_enable;
    int InstantRasterDerivedVoxelScene::dbg_vsp_warm_start_enable_ = k_default_instant_rdv_param.vsp_warm_start_enable;
    int InstantRasterDerivedVoxelScene::dbg_vsp_probe_pool_size_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_free_probe_count_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_allocated_probe_count_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_active_probe_count_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_vsp_visible_surface_cell_count_ = 0;
    bool InstantRasterDerivedVoxelScene::dbg_main_view_reduced_surface_enable_ = true;
    bool InstantRasterDerivedVoxelScene::dbg_vsp_debug_readback_enable_ = true;
    int InstantRasterDerivedVoxelScene::dbg_assp_total_ray_count_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_assp_probe_count_ = 0;
    int InstantRasterDerivedVoxelScene::dbg_gi_update_sample_mode_ = static_cast<int>(InstantRdvGiSolutionMode::Vsp);
    float InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_default_ = k_occupancy_injection_default_fine_cells;
    float InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_ = dbg_bbv_occupancy_injection_fine_cells_default_;

    void InstantRasterDerivedVoxelScene::DrawDebugMenu(
        bool* p_enable_all_injection,
        bool* p_enable_all_removal,
        bool* p_enable_main_view_injection,
        bool* p_enable_main_view_removal,
        bool* p_enable_shadow_view_injection,
        bool* p_enable_shadow_view_removal)
    {
        if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings", "InstantRdv"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);

            // 右クリックで個別リセット. BeginPopupContextItem は直前のウィジェットを対象とする.
            ImGui::Checkbox("All Occupancy Injection (AND)", p_enable_all_injection);
            ImGui::Checkbox("All Removal (AND)", p_enable_all_removal);
            ImGui::Checkbox("MainView Occupancy Injection", p_enable_main_view_injection);
            ImGui::Checkbox("MainView Removal", p_enable_main_view_removal);
            ImGui::Checkbox("ShadowView Occupancy Injection", p_enable_shadow_view_injection);
            ImGui::Checkbox("ShadowView Removal", p_enable_shadow_view_removal);
            ImGui::SliderFloat("BBV Occupancy Injection Offset (fine cells)", &InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_, 0.0f, 8.0f, "%.2f");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Reset to Default"))
                    InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_ = InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_default_;
                ImGui::EndPopup();
            }
            ImGui::TextDisabled(
                "Default: %.2f fine cells",
                InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_default_);
            ImGui::Checkbox(
                "MainView Reduced Surface Buffer",
                &InstantRasterDerivedVoxelScene::dbg_main_view_reduced_surface_enable_);
            ImGui::TextDisabled(
                "1/4 x 1/4 jittered Depth + approximate normal for Occupancy/VSP/Radiance.");
            ImGui::TextDisabled(
                "Carving and ShadowView keep the full-resolution Legacy path.");
            ImGui::TextDisabled("Removal Cull: conservative world-space Brick AABB");
            ImGui::Text("GI Update Target (linked): %s", InstantRdvGiSolutionModeName(dbg_gi_update_sample_mode_));

            
            if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/Assp", "Adaptive Screen Space Probe"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                NGL_IMGUI_SCOPED_ID("ASSP");

                {
                    bool v = (0 != assp_spatial_filter_enable_);
                    if (ImGui::Checkbox("SpatialFilter", &v))
                        assp_spatial_filter_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_spatial_filter_enable_ = k_default_instant_rdv_param.assp_spatial_filter_enable;
                        ImGui::EndPopup();
                    }
                }
                ImGui::SliderFloat("Spatial Filter Normal Cos Threshold", &assp_spatial_filter_normal_cos_threshold_, -1.0f, 1.0f, "%.4f");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_spatial_filter_normal_cos_threshold_ = k_default_instant_rdv_param.assp_spatial_filter_normal_cos_threshold;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Spatial Filter Depth Exp Scale", &assp_spatial_filter_depth_exp_scale_, 0.0f, 500.0f, "%.4f");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_spatial_filter_depth_exp_scale_ = k_default_instant_rdv_param.assp_spatial_filter_depth_exp_scale;
                    ImGui::EndPopup();
                }
                {
                    bool v = (0 != assp_temporal_reprojection_enable_);
                    if (ImGui::Checkbox("TemporalReprojection", &v))
                        assp_temporal_reprojection_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_temporal_reprojection_enable_ = k_default_instant_rdv_param.assp_temporal_reprojection_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != assp_ray_guiding_enable_);
                    if (ImGui::Checkbox("RayGuiding", &v))
                        assp_ray_guiding_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            assp_ray_guiding_enable_ = k_default_instant_rdv_param.assp_ray_guiding_enable;
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
                        assp_ray_budget_min_rays_ = k_default_instant_rdv_param.assp_ray_budget_min_rays;
                    ImGui::EndPopup();
                }
                ImGui::SliderInt("Max Rays", &assp_ray_budget_max_rays_, 1, k_assp_ray_budget_ui_max_rays);
                show_ray_budget_tooltip("Per-probe ray count upper bound. Values >16 are allowed, while total frame rays remain capped to (probe_count * 16).");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_max_rays_ = k_default_instant_rdv_param.assp_ray_budget_max_rays;
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
                        assp_ray_budget_variance_weight_ = k_default_instant_rdv_param.assp_ray_budget_variance_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Normal Delta Weight", &assp_ray_budget_normal_delta_weight_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Weight of normal change between current tile and best previous tile.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_normal_delta_weight_ = k_default_instant_rdv_param.assp_ray_budget_normal_delta_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Depth Delta Weight", &assp_ray_budget_depth_delta_weight_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Weight of depth change between current tile and best previous tile.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_depth_delta_weight_ = k_default_instant_rdv_param.assp_ray_budget_depth_delta_weight;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget No History Bias", &assp_ray_budget_no_history_bias_, 0.0f, 2.0f, "%.4f");
                show_ray_budget_tooltip("Additional score when no valid temporal history exists. Raises rays for newly observed probes.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_no_history_bias_ = k_default_instant_rdv_param.assp_ray_budget_no_history_bias;
                    ImGui::EndPopup();
                }
                ImGui::SliderFloat("Budget Scale", &assp_ray_budget_scale_, 0.0f, 32.0f, "%.4f");
                show_ray_budget_tooltip("Pre-scale for variance signal before weighting. Higher values make ray distribution react faster.");
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reset to Default"))
                        assp_ray_budget_scale_ = k_default_instant_rdv_param.assp_ray_budget_scale;
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
                            assp_debug_freeze_frame_random_enable_ = k_default_instant_rdv_param.assp_debug_freeze_frame_random_enable;
                        ImGui::EndPopup();
                    }
                }
            }

            if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/Vsp", "Visibility Surface Probe##InstantRdvSettingsVsp"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                {
                    bool v = (0 != dbg_vsp_lighting_interpolation_enable_);
                    if (ImGui::Checkbox("Lighting Interpolation", &v))
                        dbg_vsp_lighting_interpolation_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_lighting_interpolation_enable_ = k_default_instant_rdv_param.vsp_lighting_interpolation_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_vsp_irradiance_volume_propagate_active_probe_weight_enable_);
                    if (ImGui::Checkbox("IV Propagate ActiveProbe Weight", &v))
                        dbg_vsp_irradiance_volume_propagate_active_probe_weight_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_irradiance_volume_propagate_active_probe_weight_enable_ =
                                k_default_instant_rdv_param.vsp_irradiance_volume_propagate_active_probe_weight_enable;
                        ImGui::EndPopup();
                    }
                    ImGui::SliderFloat(
                        "IV Propagate ActiveProbe Weight Scale",
                        &dbg_vsp_irradiance_volume_propagate_active_probe_weight_scale_,
                        1.0f,
                        10.0f,
                        "%.2f");
                    dbg_vsp_irradiance_volume_propagate_active_probe_weight_scale_ = std::clamp(
                        dbg_vsp_irradiance_volume_propagate_active_probe_weight_scale_,
                        1.0f,
                        10.0f);
                    ImGui::TextDisabled("Weights direct ActiveProbe neighbors during IV propagation.");
                }
                {
                    bool v = (0 != dbg_vsp_update_ray_jitter_enable_);
                    if (ImGui::Checkbox("Update Ray Jitter (Oct Cell)", &v))
                        dbg_vsp_update_ray_jitter_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_update_ray_jitter_enable_ = k_default_instant_rdv_param.debug_vsp_update_ray_jitter_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_vsp_probe_lifecycle_enable_);
                    if (ImGui::Checkbox("Probe Lifecycle (Spawn/Relocate/Release)", &v))
                        dbg_vsp_probe_lifecycle_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_probe_lifecycle_enable_ = k_default_instant_rdv_param.vsp_probe_lifecycle_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    bool v = (0 != dbg_vsp_warm_start_enable_);
                    if (ImGui::Checkbox("Probe Warm Start", &v))
                        dbg_vsp_warm_start_enable_ = v ? 1 : 0;
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_warm_start_enable_ = k_default_instant_rdv_param.vsp_warm_start_enable;
                        ImGui::EndPopup();
                    }
                }
                {
                    ImGui::SliderFloat("Relocation Offset Scale", &dbg_vsp_relocation_offset_scale_for_cascade_cell_size_, 0.01f, 10.0f);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_vsp_relocation_offset_scale_for_cascade_cell_size_ = k_default_instant_rdv_param.vsp_relocation_offset_scale_for_cascade_cell_size;
                        ImGui::EndPopup();
                    }
                }
            }

            if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug", "Probe Debug"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug/Common", "Common"))
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
                            dbg_probe_near_geom_scale_ = k_default_instant_rdv_param.debug_probe_near_geom_scale;
                        ImGui::EndPopup();
                    }
                }

                if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug/Vsp", "Visibility Surface Probe##InstantRdvProbeDebugVsp"))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);

                    if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug/Vsp/Stats", "Stats"))
                    {
                        NGL_IMGUI_SCOPED_INDENT(10.0f);
                        ImGui::Text("Cascade Count: %d", dbg_vsp_cascade_count_);
                        ImGui::Text("Probe Pool Size: %d", dbg_vsp_probe_pool_size_);
                        ImGui::Text("Allocated Probes: %d", dbg_vsp_allocated_probe_count_);
                        ImGui::Text("Free Probes: %d", dbg_vsp_free_probe_count_);
                        ImGui::Text("Active Probes: %d", dbg_vsp_active_probe_count_);
                        ImGui::Text("Visible Surface Cells: %d", dbg_vsp_visible_surface_cell_count_);
                    }

                    if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug/Vsp/Visualization", "Visualization"))
                    {
                        NGL_IMGUI_SCOPED_INDENT(10.0f);
                        if (ImGui::SliderInt("ActiveProbe Mode", &dbg_vsp_probe_debug_mode_, -1, 9))
                            dbg_vsp_irradiance_volume_debug_mode_ = -1;
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Reset to Default"))
                                dbg_vsp_probe_debug_mode_ = k_default_instant_rdv_param.debug_vsp_probe_mode;
                            ImGui::EndPopup();
                        }
                        ImGui::TextDisabled("%s", VspProbeDebugModeLabel(dbg_vsp_probe_debug_mode_));
                        const char* vsp_probe_mode_detail = VspProbeDebugModeDetailLabel(dbg_vsp_probe_debug_mode_);
                        if (vsp_probe_mode_detail[0] != '\0')
                        {
                            ImGui::TextDisabled("%s", vsp_probe_mode_detail);
                        }
                        {
                            bool v = (0 != dbg_vsp_probe_use_relocated_pos_);
                            if (ImGui::Checkbox("Use Relocated Probe Position", &v))
                                dbg_vsp_probe_use_relocated_pos_ = v ? 1 : 0;
                            if (ImGui::BeginPopupContextItem()) {
                                if (ImGui::MenuItem("Reset to Default"))
                                    dbg_vsp_probe_use_relocated_pos_ = k_default_instant_rdv_param.debug_vsp_probe_use_relocated_pos;
                                ImGui::EndPopup();
                            }
                            ImGui::TextDisabled("ON: relocated probe position, OFF: cell center.");
                        }
                        const int vsp_debug_cascade_max = std::max(-1, dbg_vsp_cascade_count_ - 1);
                        ImGui::SliderInt("Vsp Debug Cascade", &dbg_vsp_probe_debug_cascade_, -1, vsp_debug_cascade_max);
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Reset to Default"))
                                dbg_vsp_probe_debug_cascade_ = -1;
                            ImGui::EndPopup();
                        }
                        ImGui::TextDisabled("-1 = all cascades");

                        if (ImGui::SliderInt("IrradianceVolume Mode", &dbg_vsp_irradiance_volume_debug_mode_, -1, 1))
                            dbg_vsp_probe_debug_mode_ = -1;
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Reset to Default"))
                                dbg_vsp_irradiance_volume_debug_mode_ = -1;
                            ImGui::EndPopup();
                        }
                        ImGui::TextDisabled("%s", VspIrradianceVolumeDebugModeLabel(dbg_vsp_irradiance_volume_debug_mode_));
                        bool depth_test = (0 != dbg_vsp_probe_depth_test_);
                        if (ImGui::Checkbox("Probe Depth Test", &depth_test))
                            dbg_vsp_probe_depth_test_ = depth_test ? 1 : 0;
                        ImGui::TextDisabled("ON: SceneDepth occlusion, OFF: always visible.");
                    }
                }

                if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/ProbeDebug/Bbv", "Bitmask Brick Voxel"))
                {
                    NGL_IMGUI_SCOPED_INDENT(10.0f);
                    ImGui::SliderInt("Bbv Probe Mode", &dbg_bbv_probe_debug_mode_, -1, 10);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Reset to Default"))
                            dbg_bbv_probe_debug_mode_ = k_default_instant_rdv_param.debug_bbv_probe_mode;
                        ImGui::EndPopup();
                    }
                    ImGui::TextDisabled("%s", BbvProbeDebugModeLabel(dbg_bbv_probe_debug_mode_));
                }
            }
            
            if (ngl::imgui::PersistentCollapsingHeader("DebugWindow/InstantRdv/Settings/VoxelDebug", "Voxel Debug"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);

                // カテゴリ選択ラジオボタン.
                if (ImGui::RadioButton("Off", dbg_view_category_ == -1)) { dbg_view_category_ = -1; }
                ImGui::SameLine();
                if (ImGui::RadioButton("BBV", dbg_view_category_ == 0)) { dbg_view_category_ = 0; }
                ImGui::SameLine();
                if (ImGui::RadioButton("VSP", dbg_view_category_ == 1)) { dbg_view_category_ = 1; }
                ImGui::SameLine();
                if (ImGui::RadioButton("ASSP", dbg_view_category_ == 2)) { dbg_view_category_ = 2; }
                if(dbg_view_category_ > 2) { dbg_view_category_ = 2; }

                // カテゴリ別サブモードスライダ.
                if (0 <= dbg_view_category_)
                {
                    const int k_sub_mode_max[] = { 7, 6, 7 };
                    auto get_sub_mode_description = [](int category, int sub_mode) -> const char*
                    {
                        switch(category)
                        {
                        case 0: // BBV
                            switch(sub_mode)
                            {
                            case 0: return "FineVoxel (Unique Color)";
                            case 1: return "FineVoxel (Brick Color)";
                            case 2: return "FineVoxel (Brick Radiance)";
                            case 3: return "FineVoxel HitDepth";
                            case 4: return "Brick (Brick Color)";
                            case 5: return "TopDown Density";
                            case 6: return "Reduced Surface Normal";
                            case 7: return "Reduced Surface Confidence";
                            default: return "Unknown";
                            }
                        case 1: // VSP
                            switch(sub_mode)
                            {
                            case 0: return "VSP Octahedral atlas RGBA";
                            case 1: return "VSP IrradianceVolume Z slices: coefficient 0 (legacy RGBA)";
                            case 2: return "VSP IrradianceVolume Z slices: SkyVisibility SH";
                            case 3: return "VSP IrradianceVolume Z slices: Irradiance R SH";
                            case 4: return "VSP IrradianceVolume Z slices: Irradiance G SH";
                            case 5: return "VSP IrradianceVolume Z slices: Irradiance B SH";
                            case 6: return "VSP ShadingTest (Surface)";
                            default: return "Unknown";
                            }
                        case 2: // ASSP
                            switch(sub_mode)
                            {
                            case 0: return "ASSP probe atlas raw";
                            case 1: return "ASSP packed SH raw";
                            case 2: return "ASSP SH sample";
                            case 3: return "Filtered variance mean";
                            case 4: return "Filtered variance";
                            case 5: return "Raw variance mean";
                            case 6: return "Raw variance";
                            case 7: return "Per-probe ray count";
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
                    ImGui::TextDisabled("Sub Mode %d: %s", dbg_view_sub_mode_, get_sub_mode_description(dbg_view_category_, dbg_view_sub_mode_));

                    if (1 == dbg_view_category_ && 1 <= dbg_view_sub_mode_ && dbg_view_sub_mode_ <= 5)
                    {
                        ImGui::SliderInt(
                            "Slice Scale",
                            &dbg_vsp_irradiance_volume_slice_scale_,
                            1,
                            16);
                        dbg_vsp_irradiance_volume_slice_scale_ =
                            std::clamp(dbg_vsp_irradiance_volume_slice_scale_, 1, 16);
                        const int slice_content_width = std::max(
                            static_cast<int>(dbg_vsp_resolution_.z * (dbg_vsp_resolution_.x + 1u)) - 1,
                            1);
                        const int slice_content_height = std::max(
                            dbg_vsp_cascade_count_ * static_cast<int>(dbg_vsp_resolution_.y + 1u) - 1,
                            1);
                        const ImVec2 display_size = ImGui::GetIO().DisplaySize;
                        const int visible_width = std::max(
                            static_cast<int>(display_size.x) / dbg_vsp_irradiance_volume_slice_scale_,
                            1);
                        const int visible_height = std::max(
                            static_cast<int>(display_size.y) / dbg_vsp_irradiance_volume_slice_scale_,
                            1);
                        const int slice_scroll_x_max = std::max(slice_content_width - visible_width, 0);
                        const int slice_scroll_y_max = std::max(slice_content_height - visible_height, 0);
                        dbg_vsp_irradiance_volume_slice_scroll_x_ = std::clamp(
                            dbg_vsp_irradiance_volume_slice_scroll_x_, 0, slice_scroll_x_max);
                        dbg_vsp_irradiance_volume_slice_scroll_y_ = std::clamp(
                            dbg_vsp_irradiance_volume_slice_scroll_y_, 0, slice_scroll_y_max);
                        ImGui::SliderInt(
                            "Slice Scroll X",
                            &dbg_vsp_irradiance_volume_slice_scroll_x_,
                            0,
                            slice_scroll_x_max);
                        ImGui::SliderInt(
                            "Slice Scroll Y",
                            &dbg_vsp_irradiance_volume_slice_scroll_y_,
                            0,
                            slice_scroll_y_max);
                        if (ImGui::Button("Reset Slice View"))
                        {
                            dbg_vsp_irradiance_volume_slice_scale_ =
                                k_default_instant_rdv_param.debug_vsp_irradiance_volume_slice_scale;
                            dbg_vsp_irradiance_volume_slice_scroll_x_ = 0;
                            dbg_vsp_irradiance_volume_slice_scroll_y_ = 0;
                        }
                        ImGui::TextDisabled("Z slices: left to right, Cascades: top to bottom");
                    }
                    else if (1 == dbg_view_category_ && 6 == dbg_view_sub_mode_)
                    {
                        const char* const shading_test_signal_labels[] =
                        {
                            "Irradiance",
                            "SkyVisibility",
                            "ActiveProbe Update",
                            "Cascade",
                        };
                        ImGui::Combo(
                            "Shading Test Target",
                            &dbg_vsp_shading_test_signal_,
                            shading_test_signal_labels,
                            4);
                        dbg_vsp_shading_test_signal_ = std::clamp(dbg_vsp_shading_test_signal_, 0, 3);

                        const int shading_test_cascade_max = std::max(dbg_vsp_cascade_count_ - 1, 0);
                        ImGui::SliderInt(
                            "Shading Test Cascade",
                            &dbg_vsp_shading_test_cascade_,
                            -1,
                            shading_test_cascade_max);
                        dbg_vsp_shading_test_cascade_ = std::clamp(
                            dbg_vsp_shading_test_cascade_,
                            -1,
                            shading_test_cascade_max);
                        ImGui::TextDisabled("-1: All (continuous camera-relative selection)");

                        bool trilinear_enable = (0 != dbg_vsp_shading_test_trilinear_enable_);
                        if (ImGui::Checkbox("Trilinear", &trilinear_enable))
                            dbg_vsp_shading_test_trilinear_enable_ = trilinear_enable ? 1 : 0;
                        bool cascade_interpolation_enable =
                            (0 != dbg_vsp_shading_test_cascade_interpolation_enable_);
                        if (ImGui::Checkbox("Cascade Interpolation", &cascade_interpolation_enable))
                            dbg_vsp_shading_test_cascade_interpolation_enable_ =
                                cascade_interpolation_enable ? 1 : 0;
                        ImGui::TextDisabled("Cascade Interpolation is used only with All.");

                        if (0 == dbg_vsp_shading_test_signal_)
                        {
                            ImGui::SliderFloat(
                                "Irradiance EV",
                                &dbg_vsp_shading_test_irradiance_ev_,
                                -16.0f,
                                16.0f,
                                "%.2f EV");
                            dbg_vsp_shading_test_irradiance_ev_ = std::clamp(
                                dbg_vsp_shading_test_irradiance_ev_,
                                -16.0f,
                                16.0f);
                        }
                        else if (2 == dbg_vsp_shading_test_signal_)
                        {
                            ImGui::TextDisabled("Green: ActiveProbe in representative cell / Yellow: ActiveProbe in neighbor / Black: None");
                            ImGui::TextDisabled("Checks the representative cell and 3x3x3 neighbors in the same cascade. Trilinear does not affect this mode.");
                        }
                        else if (3 == dbg_vsp_shading_test_signal_)
                        {
                            ImGui::TextDisabled("Cascade 0: Red / 1: Green / 2: Blue / 3: Yellow / 4: Magenta / 5: Cyan");
                            ImGui::TextDisabled("Shows the cascade selected after boundary dithering.");
                        }
                        ImGui::TextDisabled("Depth-derived position and approximate normal.");
                    }
                    else if (0 == dbg_view_category_)
                    {
                        bool bbv_depth_test = (0 != dbg_bbv_depth_test_enable_);
                        if (ImGui::Checkbox("Depth Test", &bbv_depth_test))
                            dbg_bbv_depth_test_enable_ = bbv_depth_test ? 1 : 0;
                        ImGui::TextDisabled("ON: occlusion test with MainView Depth SRV / OFF: always visible");
                        if(dbg_view_sub_mode_ == 6 || dbg_view_sub_mode_ == 7)
                        {
                            ImGui::TextDisabled(
                                "Requires MainView Reduced Surface Buffer.");
                        }
                    }
                }
            }
        }

    }

    using InstantRdvShaderBindName = ngl::text::HashText<128>;
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_atlas_srv = "VspProbeAtlasTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_atlas_uav = "RWVspProbeAtlasTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_irradiance_volume_sh_srv = "VspIrradianceVolumeSHTexture";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_irradiance_volume_sh_uav = "RWVspIrradianceVolumeSHTexture";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_probe_ray_request_srv = "VspProbeRayRequestBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_probe_ray_request_uav = "RWVspProbeRayRequestBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_probe_trace_indirect_arg_uav = "RWVspProbeTraceIndirectArg";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_probe_ray_result_srv = "VspProbeRayResultBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_vsp_probe_ray_result_uav = "RWVspProbeRayResultBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_srv = "AdaptiveScreenSpaceProbeTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_history_srv = "AdaptiveScreenSpaceProbeHistoryTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_uav = "RWAdaptiveScreenSpaceProbeTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_tile_info_srv = "AdaptiveScreenSpaceProbeTileInfoTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_history_tile_info_srv = "AdaptiveScreenSpaceProbeHistoryTileInfoTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_tile_info_uav = "RWAdaptiveScreenSpaceProbeTileInfoTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_best_prev_tile_srv = "AdaptiveScreenSpaceProbeBestPrevTileTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_best_prev_tile_uav = "RWAdaptiveScreenSpaceProbeBestPrevTileTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_variance_srv = "AdaptiveScreenSpaceProbeVarianceTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_history_variance_srv = "AdaptiveScreenSpaceProbeHistoryVarianceTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_variance_uav = "RWAdaptiveScreenSpaceProbeVarianceTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_filtered_uav = "RWAdaptiveScreenSpaceProbeFilteredTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_packed_sh_srv = "AdaptiveScreenSpaceProbePackedSHTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_asspprobe_packed_sh_uav = "RWAdaptiveScreenSpaceProbePackedSHTex";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_trace_indirect_arg_uav = "RWAsspProbeTraceIndirectArg";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_total_ray_count_srv = "AsspProbeTotalRayCountBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_total_ray_count_uav = "RWAsspProbeTotalRayCountBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_meta_srv = "AsspProbeRayMetaBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_meta_uav = "RWAsspProbeRayMetaBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_query_srv = "AsspProbeRayQueryBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_query_uav = "RWAsspProbeRayQueryBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_result_srv = "AsspProbeRayResultBuffer";
    constexpr InstantRdvShaderBindName k_shader_bind_name_assp_probe_ray_result_uav = "RWAsspProbeRayResultBuffer";

    ngl::rhi::ConstantBufferPooledHandle AllocInstantRdvParamCbh(
        ngl::rhi::GraphicsCommandListDep* p_command_list,
        const InstantRdvParam& param)
    {
        auto cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(InstantRdvParam));
        auto* p_mapped = cbh->buffer.MapAs<InstantRdvParam>();
        std::memcpy(p_mapped, &param, sizeof(InstantRdvParam));
        cbh->buffer.Unmap();
        return cbh;
    }
    void ToroidalGridUpdater::Initialize(const math::Vec3u& grid_resolution, float bbv_cell_size)
    {
        grid_.resolution = grid_resolution;
        grid_.cell_size = bbv_cell_size;
        grid_.cell_size_inv = 1.0f / bbv_cell_size;

        const u32 total_count = grid_.resolution.x * grid_.resolution.y * grid_.resolution.z;
        grid_.total_count = total_count;
        grid_.flatten_2d_width = static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(total_count))));
    }
    void ToroidalGridUpdater::UpdateGrid(const math::Vec3& important_pos)
    {
        // 中心をマイナス無限方向へ丸めた離散CELLIDで保持.
        grid_.center_cell_id_prev = grid_.center_cell_id;
        grid_.center_cell_id      = math::Vec3::Floor(important_pos * grid_.cell_size_inv).Cast<int>();

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
        const int assp_probe_base_resolution_x = std::max(render_resolution.x, 1);
        const int assp_probe_base_resolution_y = std::max(render_resolution.y, 1);

        for(auto& tex : assp_probe_tex_) { tex = {}; }
        for(auto& tex : assp_probe_tile_info_tex_) { tex = {}; }
        assp_probe_packed_sh_tex_ = {};
        assp_probe_best_prev_tile_tex_ = {};
        assp_probe_trace_indirect_arg_ = {};
        assp_probe_total_ray_count_buffer_ = {};
        assp_probe_ray_meta_buffer_ = {};
        assp_probe_ray_query_buffer_ = {};
        assp_probe_ray_result_buffer_ = {};
        assp_probe_total_ray_count_readback_buffer_ = {};
        reduced_surface_buffer_tex_ = {};
        vsp_surface_cell_mask_buffer_ = {};

        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =
                (assp_probe_base_resolution_x +
                 k_reduced_surface_buffer_downscale - 1) /
                k_reduced_surface_buffer_downscale;
            desc.height =
                (assp_probe_base_resolution_y +
                 k_reduced_surface_buffer_downscale - 1) /
                k_reduced_surface_buffer_downscale;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R32G32B32A32_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag =
                rhi::ResourceBindFlag::ShaderResource |
                rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;

            if(!reduced_surface_buffer_tex_.Initialize(
                p_device,
                desc,
                "InstantRdv_ReducedSurfaceBuffer"))
            {
                return false;
            }
        }
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

            if(!assp_probe_tex_[i].Initialize(p_device, desc, (0 == i) ? "InstantRdv_AsspProbeTexA" : "InstantRdv_AsspProbeTexB"))
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

            if(!assp_probe_variance_tex_[i].Initialize(p_device, desc, (0 == i) ? "InstantRdv_AsspProbeVarianceTexA" : "InstantRdv_AsspProbeVarianceTexB"))
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

            if(!assp_probe_tile_info_tex_[i].Initialize(p_device, desc, (0 == i) ? "InstantRdv_AsspProbeTileInfoTexA" : "InstantRdv_AsspProbeTileInfoTexB"))
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

            if(!assp_probe_packed_sh_tex_.Initialize(p_device, desc, "InstantRdv_AsspProbePackedShTex"))
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

            if(!assp_probe_best_prev_tile_tex_.Initialize(p_device, desc, "InstantRdv_AsspProbeBestPrevTileTex"))
                return false;
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
                "InstantRdv_AsspProbeTraceIndirectArg"))
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
                "InstantRdv_AsspProbeTotalRayCount"))
            {
                return false;
            }
            if(!InitializeReadbackBuffer(
                p_device,
                assp_probe_total_ray_count_readback_buffer_,
                assp_probe_total_ray_count_buffer_.buffer->GetDesc(),
                "InstantRdv_AsspProbeTotalRayCountReadback"))
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
                "InstantRdv_AsspProbeRayMetaBuffer"))
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
                "InstantRdv_AsspProbeRayQueryBuffer"))
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
                "InstantRdv_AsspProbeRayResultBuffer"))
            {
                return false;
            }
        }
        {
            vsp_surface_mask_word_count_ = 0u;
            for(const auto& cascade_grid_updater : vsp_grid_updaters_)
            {
                const auto& resolution = cascade_grid_updater.Get().resolution;
                const u32 brick_count_x =
                    (resolution.x + k_vsp_surface_mask_brick_resolution - 1u) /
                    k_vsp_surface_mask_brick_resolution;
                const u32 brick_count_y =
                    (resolution.y + k_vsp_surface_mask_brick_resolution - 1u) /
                    k_vsp_surface_mask_brick_resolution;
                const u32 brick_count_z =
                    (resolution.z + k_vsp_surface_mask_brick_resolution - 1u) /
                    k_vsp_surface_mask_brick_resolution;
                const uint64_t cascade_word_count =
                    uint64_t(brick_count_x) *
                    uint64_t(brick_count_y) *
                    uint64_t(brick_count_z) *
                    uint64_t(k_vsp_surface_mask_brick_word_count);
                if(cascade_word_count > uint64_t(std::numeric_limits<u32>::max()) -
                    uint64_t(vsp_surface_mask_word_count_))
                {
                    std::cout << "[ERROR] VSP SurfaceCellMask word count overflows u32." << std::endl;
                    return false;
                }
                vsp_surface_mask_word_count_ += static_cast<u32>(cascade_word_count);
            }
            vsp_surface_mask_word_count_ = std::max<u32>(1u, vsp_surface_mask_word_count_);
            if(!vsp_surface_cell_mask_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = vsp_surface_mask_word_count_,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspSurfaceCellMaskBuffer"))
            {
                return false;
            }
        }
        {
            if(!vsp_probe_ray_request_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = (vsp_probe_pool_size_ * k_vsp_probe_octmap_width * k_vsp_probe_octmap_width) + 1u, // 0 is atomic counter.
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspProbeRayRequestBuffer"))
            {
                return false;
            }
        }
        {
            if(!vsp_probe_trace_indirect_arg_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 3,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspProbeTraceIndirectArg"))
            {
                return false;
            }
        }
        {
            if(!vsp_probe_resolve_indirect_arg_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 3,
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspProbeResolveIndirectArg"))
            {
                return false;
            }
        }
        {
            constexpr u32 k_vsp_ray_result_stride = 2u; // packed request key + hit info.
            const u32 ray_count_per_probe = k_vsp_probe_octmap_width * k_vsp_probe_octmap_width;
            const u32 max_ray_count = vsp_probe_pool_size_ * ray_count_per_probe;
            if(!vsp_probe_ray_result_buffer_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count     = 1u + max_ray_count * k_vsp_ray_result_stride, // 0 is atomic counter.
                    .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspProbeRayResultBuffer"))
            {
                return false;
            }
        }
        return true;
    }

    // 初期化
    bool BitmaskBrickVoxelGi::Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg)
    {
        if(!p_device)
        {
            std::cout << "[ERROR] Invalid VSP configuration: device is null." << std::endl;
            return false;
        }
        if(!ValidateVspInitArg(init_arg))
        {
            return false;
        }
        char* enable_vsp_debug_readback = nullptr;
        size_t enable_vsp_debug_readback_length = 0;
        if(_dupenv_s(
               &enable_vsp_debug_readback,
               &enable_vsp_debug_readback_length,
               "NGL_VSP_DEBUG_READBACK") == 0 &&
           enable_vsp_debug_readback != nullptr)
        {
            InstantRasterDerivedVoxelScene::dbg_vsp_debug_readback_enable_ =
                std::atoi(enable_vsp_debug_readback) != 0;
        }
        std::free(enable_vsp_debug_readback);

        bbv_grid_updater_.Initialize(init_arg.voxel_resolution, init_arg.voxel_size);

        vsp_cascade_count_ = init_arg.probe_cascade_count;
        vsp_grid_updaters_.resize(vsp_cascade_count_);
        vsp_cascade_cell_offset_array_.resize(vsp_cascade_count_);
        vsp_total_cell_count_ = 0;
        {
            float cascade_cell_size = init_arg.probe_cell_size;
            for(u32 cascade_index = 0; cascade_index < vsp_cascade_count_; ++cascade_index)
            {
                vsp_grid_updaters_[cascade_index].Initialize(init_arg.probe_resolution, cascade_cell_size);
                vsp_cascade_cell_offset_array_[cascade_index] = vsp_total_cell_count_;
                vsp_total_cell_count_ += vsp_grid_updaters_[cascade_index].Get().total_count;
                cascade_cell_size *= 2.0f;
            }
        }
        {
            const u32 expected_cell_count_per_cascade =
                init_arg.probe_resolution.x *
                init_arg.probe_resolution.y *
                init_arg.probe_resolution.z;
            float expected_cell_size = init_arg.probe_cell_size;
            for(u32 cascade_index = 0; cascade_index < vsp_cascade_count_; ++cascade_index)
            {
                const auto& cascade_grid = vsp_grid_updaters_[cascade_index].Get();
                const bool has_expected_layout =
                    cascade_grid.resolution.x == init_arg.probe_resolution.x &&
                    cascade_grid.resolution.y == init_arg.probe_resolution.y &&
                    cascade_grid.resolution.z == init_arg.probe_resolution.z &&
                    cascade_grid.total_count == expected_cell_count_per_cascade &&
                    vsp_cascade_cell_offset_array_[cascade_index] ==
                        cascade_index * expected_cell_count_per_cascade &&
                    cascade_grid.cell_size == expected_cell_size;
                if(!has_expected_layout)
                {
                    std::cout
                        << "[ERROR] Invalid VSP configuration: cascade layout no longer matches "
                        << "the equal-resolution, contiguous-offset, 2x-scale IrradianceVolume assumptions."
                        << std::endl;
                    return false;
                }
                expected_cell_size *= 2.0f;
            }
        }


        const auto bbv_grid_resolution = bbv_grid_updater_.Get().resolution;
        const u32 voxel_count = bbv_grid_resolution.x * bbv_grid_resolution.y * bbv_grid_resolution.z;
        // BBV本体バッファは shader 側と同じく
        //   [bitmask region][brick data region]
        // の順で単一の R32_UINT バッファへ確保する。
        // bitmask は Brick ごとの固定長、brick data は固定長配列として積み上げる。
        const u32 bbv_buffer_element_count =
            voxel_count * k_bbv_per_voxel_bitmask_u32_count +
            voxel_count * k_bbv_brick_data_u32_count;
        // サーフェイスVoxelのリスト. スクリーン上でサーフェイスとして充填された要素を詰め込む. Bbvの充填とは別で, 後処理でサーフェイスVoxelを処理するためのリスト.
        bbv_fine_update_voxel_count_max_= std::clamp(voxel_count / 50u, 64u, k_max_update_probe_work_count);

        // 中空Voxelのクリアキューサイズ. スクリーン上で中空判定された要素を詰め込む.
        bbv_hollow_voxel_list_count_max_= 1024*2;


        vsp_visible_surface_buffer_size_ = k_vsp_probe_surface_cell_count_max;

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
            pso_bbv_clear_  = CreateComputePSO("instant_rdv/bbv/bbv_clear_voxel_cs.hlsl");
            pso_bbv_begin_update_ = CreateComputePSO("instant_rdv/bbv/bbv_begin_update_cs.hlsl");
            pso_bbv_begin_view_update_ = CreateComputePSO("instant_rdv/bbv/bbv_begin_view_update_cs.hlsl");
            pso_reduced_surface_buffer_build_ = CreateComputePSO("instant_rdv/reduced_surface_buffer_build_cs.hlsl");
            pso_bbv_radiance_injection_apply_short_ray_ = CreateComputePSO("instant_rdv/bbv/bbv_radiance_injection_apply_short_ray_cs.hlsl");
            pso_bbv_radiance_injection_apply_reduced_surface_ = CreateComputePSO("instant_rdv/bbv/bbv_radiance_injection_apply_reduced_surface_cs.hlsl");
            pso_bbv_radiance_resolve_ = CreateComputePSO("instant_rdv/bbv/bbv_radiance_resolve_cs.hlsl");
            pso_bbv_brick_count_aggregate_ = CreateComputePSO("instant_rdv/bbv/bbv_brick_count_aggregate_cs.hlsl");
            pso_bbv_element_update_ = CreateComputePSO("instant_rdv/bbv/bbv_element_update_cs.hlsl");
            pso_bbv_removal_frustum_cull_ = CreateComputePSO("instant_rdv/bbv/bbv_removal_frustum_cull_cs.hlsl");
            pso_bbv_removal_carving_indirect_arg_build_ = CreateComputePSO("instant_rdv/bbv/bbv_removal_carving_indirect_arg_build_cs.hlsl");
            pso_bbv_occupancy_injection_apply_ = CreateComputePSO("instant_rdv/bbv/bbv_occupancy_injection_apply_cs.hlsl");
            pso_bbv_occupancy_injection_apply_integrated_surface_ = CreateComputePSO("instant_rdv/bbv/bbv_occupancy_injection_apply_integrated_surface_cs.hlsl");
            pso_bbv_occupancy_injection_apply_reduced_surface_ = CreateComputePSO("instant_rdv/bbv/bbv_occupancy_injection_apply_reduced_surface_cs.hlsl");
            pso_bbv_removal_carving_ = CreateComputePSO("instant_rdv/bbv/bbv_removal_carving_cs.hlsl");

            pso_vsp_clear_ = CreateComputePSO("instant_rdv/vsp/vsp_clear_voxel_cs.hlsl");
            pso_vsp_begin_update_ = CreateComputePSO("instant_rdv/vsp/vsp_begin_update_cs.hlsl");
            pso_vsp_debug_stats_collect_ = CreateComputePSO("instant_rdv/vsp/vsp_debug_stats_collect_cs.hlsl");
            pso_vsp_surface_mask_clear_ = CreateComputePSO("instant_rdv/vsp/vsp_surface_mask_clear_cs.hlsl");
            pso_vsp_surface_mask_compact_ = CreateComputePSO("instant_rdv/vsp/vsp_surface_mask_compact_cs.hlsl");
            pso_vsp_surface_detect_reduced_ = CreateComputePSO("instant_rdv/vsp/vsp_surface_detect_reduced_cs.hlsl");
            pso_vsp_generate_indirect_arg_ = CreateComputePSO("instant_rdv/vsp/vsp_generate_indirect_arg_cs.hlsl");
            pso_vsp_generate_prev_active_indirect_arg_ = CreateComputePSO("instant_rdv/vsp/vsp_generate_prev_active_indirect_arg_cs.hlsl");
            pso_vsp_generate_curr_active_indirect_arg_ = CreateComputePSO("instant_rdv/vsp/vsp_generate_curr_active_indirect_arg_cs.hlsl");
            pso_vsp_pre_update_ = CreateComputePSO("instant_rdv/vsp/vsp_pre_update_cs.hlsl");
            pso_vsp_probe_ray_request_ = CreateComputePSO("instant_rdv/vsp/vsp_probe_ray_request_cs.hlsl");
            pso_vsp_probe_finalize_linear_indirect_arg_ = CreateComputePSO("instant_rdv/vsp/vsp_probe_finalize_linear_indirect_arg_cs.hlsl");
            pso_vsp_probe_ray_trace_ = CreateComputePSO("instant_rdv/vsp/vsp_probe_ray_trace_cs.hlsl");
            pso_vsp_probe_ray_resolve_ = CreateComputePSO("instant_rdv/vsp/vsp_probe_ray_resolve_cs.hlsl");
            pso_vsp_sh_update_ = CreateComputePSO("instant_rdv/vsp/vsp_probe_sh_update_cs.hlsl");
            pso_vsp_irradiance_volume_propagate_ = CreateComputePSO("instant_rdv/vsp/vsp_irradiance_volume_propagate_cs.hlsl");

            pso_assp_probe_clear_ = CreateComputePSO("instant_rdv/assp/assp_probe_clear_cs.hlsl");
            pso_assp_probe_begin_ = CreateComputePSO("instant_rdv/assp/assp_probe_begin_cs.hlsl");
            pso_assp_probe_preupdate_ = CreateComputePSO("instant_rdv/assp/assp_probe_preupdate_cs.hlsl");
            pso_assp_probe_build_ray_meta_ = CreateComputePSO("instant_rdv/assp/assp_probe_build_ray_meta_cs.hlsl");
            pso_assp_probe_finalize_ray_query_ = CreateComputePSO("instant_rdv/assp/assp_probe_finalize_ray_query_cs.hlsl");
            pso_assp_probe_trace_ = CreateComputePSO("instant_rdv/assp/assp_probe_trace_cs.hlsl");
            pso_assp_probe_update_ = CreateComputePSO("instant_rdv/assp/assp_probe_update_cs.hlsl");
            pso_assp_probe_spatial_filter_ = CreateComputePSO("instant_rdv/assp/assp_probe_spatial_filter_cs.hlsl");
            pso_assp_probe_variance_ = CreateComputePSO("instant_rdv/assp/assp_probe_variance_cs.hlsl");
            pso_assp_probe_sh_update_ = CreateComputePSO("instant_rdv/assp/assp_probe_sh_update_cs.hlsl");

            // デバッグ用PSO.
            {
                pso_bbv_debug_visualize_ = CreateComputePSO("instant_rdv/debug_util/voxel_debug_visualize_cs.hlsl");
                
                {
                    pso_bbv_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                    ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                    {
                        ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                        vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                        vs_load_desc.shader_model_version          = k_shader_model;
                        vs_load_desc.entry_point_name              = "main_vs";
                        auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("instant_rdv/debug_util/voxel_probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("instant_rdv/debug_util/voxel_probe_debug_ps.hlsl"), &ps_load_desc);
                        gpso_desc.ps = &ps_load_handle->data_;
                    }

                    gpso_desc.num_render_targets = 1;
                    gpso_desc.render_target_formats[0] = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;

                    gpso_desc.depth_stencil_state.depth_enable = true;
                    gpso_desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
                    gpso_desc.depth_stencil_state.depth_write_enable = false;
                    gpso_desc.depth_stencil_state.stencil_enable = false;
                    gpso_desc.depth_stencil_format = rhi::EResourceFormat::Format_D32_FLOAT;
                    
                    auto* pso_cache = p_device->GetPipelineStateCache();
                    pso_bbv_debug_probe_ = pso_cache->GetOrCreate(p_device, gpso_desc);
                }
                {
                    pso_vsp_debug_probe_ = ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep>(new ngl::rhi::GraphicsPipelineStateDep());
                    ngl::rhi::GraphicsPipelineStateDep::Desc gpso_desc = {};
                    {
                        ngl::gfx::ResShader::LoadDesc vs_load_desc = {};
                        vs_load_desc.stage                         = ngl::rhi::EShaderStage::Vertex;
                        vs_load_desc.shader_model_version          = k_shader_model;
                        vs_load_desc.entry_point_name              = "main_vs";
                        auto vs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("instant_rdv/debug_util/probe_debug_vs.hlsl"), &vs_load_desc);
                        gpso_desc.vs = &vs_load_handle->data_;
                    }
                    {
                        ngl::gfx::ResShader::LoadDesc ps_load_desc = {};
                        ps_load_desc.stage                         = ngl::rhi::EShaderStage::Pixel;
                        ps_load_desc.shader_model_version          = k_shader_model;
                        ps_load_desc.entry_point_name              = "main_ps";
                        auto ps_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                            p_device, NGL_RENDER_SHADER_PATH("instant_rdv/debug_util/probe_debug_ps.hlsl"), &ps_load_desc);
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
                    pso_vsp_debug_probe_ = pso_cache->GetOrCreate(p_device, gpso_desc);
                    gpso_desc.depth_stencil_state.depth_enable = false;
                    pso_vsp_debug_probe_no_depth_ = pso_cache->GetOrCreate(p_device, gpso_desc);
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
                                            ,   "InstantRdv_BbvOptionalDataBuffer");
        }
        {
            bbv_radiance_accum_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = voxel_count * k_bbv_radiance_accum_component_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "InstantRdv_BbvRadianceAccumBuffer");
        }
        {
            bbv_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = bbv_buffer_element_count,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "InstantRdv_BbvBuffer");
        }
        {
            bbv_removal_frustum_brick_list_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                              .element_count     = bbv_grid_updater_.Get().total_count + 1,// 0 はcounter, 1..N は active voxel index.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "InstantRdv_BbvRemovalFrustumBrickList");
        }
        {
            bbv_removal_frustum_indirect_arg_.InitializeAsTyped(p_device,
                                          rhi::BufferDep::Desc{
                                              .element_byte_size = sizeof(uint32_t),
                                              .element_count     = 3,

                                              .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                              .heap_type = rhi::EResourceHeapType::Default},
                                          rhi::EResourceFormat::Format_R32_UINT
                                        ,   "InstantRdv_BbvRemovalFrustumIndirectArg");
        }

        {
            // V1 VSP lifecycle: cell -> probe index only.
            vsp_cell_probe_index_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = vsp_total_cell_count_,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "InstantRdv_VspCellProbeIndexBuffer");
        }
        {
            vsp_probe_pool_size_ = k_vsp_probe_pool_size;
            const auto NextPow2 = [](u32 value) -> u32
            {
                u32 result = 1;
                while (result < value)
                {
                    result <<= 1;
                }
                return result;
            };
            vsp_probe_atlas_tile_width_ = NextPow2(static_cast<u32>(std::ceil(std::sqrt(static_cast<float>(vsp_probe_pool_size_)))));
            vsp_probe_atlas_tile_height_ = (vsp_probe_pool_size_ + vsp_probe_atlas_tile_width_ - 1) / vsp_probe_atlas_tile_width_;
            vsp_probe_pool_buffer_.InitializeAsStructured(p_device,
                                           rhi::BufferDep::Desc{
                                                .element_byte_size = sizeof(VspProbePoolData),
                                               .element_count     = vsp_probe_pool_size_,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default}
                                            ,  "InstantRdv_VspProbePoolBuffer");
        }
        {
            vsp_probe_free_stack_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = vsp_probe_pool_size_ + 1, // 0番はstack counter/head用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  "InstantRdv_VspProbeFreeStack");
        }
        for (u32 active_list_index = 0; active_list_index < 2; ++active_list_index)
        {
            const std::string resource_name = std::string("InstantRdv_VspActiveProbeList") + std::to_string(active_list_index);
            // ActiveProbeListは先頭2ワードを世代交代するcounterに使用し、ワード2以降をProbe index listに使用する。
            // Current/Previousの同一Dispatch内resetとappendの競合を避けるため、未使用counterを次回用に先行クリアする。
            vsp_active_probe_list_[active_list_index].InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = vsp_probe_pool_size_ + 2, // 0,1番は交互counter.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,  resource_name.c_str());
        }
        {
            vsp_visible_surface_list_.InitializeAsTyped(p_device,
                                          rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = vsp_visible_surface_buffer_size_+1,// 0番目にアトミックカウンタ用途.

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "InstantRdv_VspVisibleSurfaceList");
        }
        {
            if(!vsp_visible_surface_source_texel_list_.InitializeAsTyped(
                p_device,
                rhi::BufferDep::Desc{
                    .element_byte_size = sizeof(uint32_t),
                    .element_count = vsp_visible_surface_buffer_size_ + 1,
                    .bind_flag =
                        rhi::ResourceBindFlag::ShaderResource |
                        rhi::ResourceBindFlag::UnorderedAccess,
                    .heap_type = rhi::EResourceHeapType::Default},
                rhi::EResourceFormat::Format_R32_UINT,
                "InstantRdv_VspVisibleSurfaceSourceTexelList"))
            {
                return false;
            }
        }
        {
            if(!vsp_debug_stats_buffer_.InitializeAsTyped(
                   p_device,
                   rhi::BufferDep::Desc{
                       .element_byte_size = sizeof(uint32_t),
                       .element_count = 3u,
                       .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                       .heap_type = rhi::EResourceHeapType::Default},
                   rhi::EResourceFormat::Format_R32_UINT,
                   "InstantRdv_VspDebugStats"))
            {
                return false;
            }
            if(!InitializeReadbackBuffer(
                   p_device,
                   vsp_debug_stats_readback_buffer_,
                   vsp_debug_stats_buffer_.buffer->GetDesc(),
                   "InstantRdv_VspDebugStatsReadback"))
            {
                return false;
            }
        }
        {
            vsp_indirect_arg_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = sizeof(uint32_t),
                                               .element_count     = 3,

                                                .bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg,
                                                .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT
                                        ,   "InstantRdv_VspIndirectArg");
        }

        // VSP プローブアトラス.
        // ActiveProbeのray resolve履歴専用。最終シェーディング用SHは下のdense IrradianceVolumeへ書き出す。
        {
            rhi::TextureDep::Desc desc = {};
            desc.type = rhi::ETextureType::Texture2D;
            desc.width =  vsp_probe_atlas_tile_width_ * k_vsp_probe_octmap_width;
            desc.height = vsp_probe_atlas_tile_height_ * k_vsp_probe_octmap_width;
            desc.depth = 1;
            desc.mip_count = 1;
            desc.array_size = 1;
            desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
            desc.sample_count = 1;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.initial_state = rhi::EResourceState::Common;// Enhanced Barrier移行時はCommonのみ許可.

            vsp_probe_atlas_tex_.Initialize(p_device, desc, "InstantRdv_VspProbeAtlasTex");
        }
        // Visibility Surface Probe IrradianceVolume SH 3D Texture.
        // Z方向へCascadeごとのSkyVisibility/Irradiance RGBサブボリュームを連結する.
        rhi::TextureDep::Desc irradiance_volume_desc = {};
        irradiance_volume_desc.type = rhi::ETextureType::Texture3D;
        irradiance_volume_desc.width = init_arg.probe_resolution.x +
            k_vsp_irradiance_volume_guard_texel_count;
        irradiance_volume_desc.height = init_arg.probe_resolution.y +
            k_vsp_irradiance_volume_guard_texel_count;
        irradiance_volume_desc.depth =
            (init_arg.probe_resolution.z + k_vsp_irradiance_volume_guard_texel_count) * vsp_cascade_count_ *
            k_vsp_irradiance_volume_sh_texture_count;
        irradiance_volume_desc.mip_count = 1;
        irradiance_volume_desc.array_size = 1;
        irradiance_volume_desc.format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
        irradiance_volume_desc.sample_count = 1;
        irradiance_volume_desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
        irradiance_volume_desc.initial_state = rhi::EResourceState::Common;
        vsp_irradiance_volume_sh_texture_.Initialize(
            p_device,
            irradiance_volume_desc,
            "InstantRdv_VspIrradianceVolumeSHTexture");

        if(!ResizeScreenProbeResources(p_device, math::Vec2i(1920, 1080)))
        {
            return false;
        }
        return true;
    }

    bool BitmaskBrickVoxelGi::PrepareFrame(
        rhi::DeviceDep* p_device,
        const math::Vec3& important_pos,
        const math::Vec3& important_dir,
        const math::Vec3& main_light_dir,
        const math::Vec2i& render_resolution,
        int gi_sample_mode)
    {
        // モード切替はRenderThread起動前に確定し、復帰するGIの時間的状態を破棄する。
        const bool is_gi_sample_mode_changed =
            (gi_sample_mode != selected_gi_sample_mode_);
        selected_gi_sample_mode_ = gi_sample_mode;

        important_point_ = important_pos;
        important_dir_ = important_dir;

        const math::Vec2i desired_probe_resolution(std::max(render_resolution.x, 1), std::max(render_resolution.y, 1));
        const bool needs_screen_probe_resize =
            (nullptr == assp_probe_tex_[0].texture.Get()) ||
            (static_cast<int>(assp_probe_tex_[0].texture->GetWidth()) != desired_probe_resolution.x) ||
            (static_cast<int>(assp_probe_tex_[0].texture->GetHeight()) != desired_probe_resolution.y);
        if(needs_screen_probe_resize)
        {
            const bool resize_success = ResizeScreenProbeResources(p_device, desired_probe_resolution);
            assert(resize_success);
            if(!resize_success)
            {
                return false;
            }

            is_first_dispatch_ = true;
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

        assp_prev_frame_tex_index_ = assp_curr_frame_tex_index_;
        assp_curr_frame_tex_index_ = 1 - assp_prev_frame_tex_index_;
        assp_latest_filtered_frame_tex_index_ = assp_prev_frame_tex_index_;
        assp_variance_prev_frame_tex_index_ = assp_variance_curr_frame_tex_index_;
        assp_variance_curr_frame_tex_index_ = 1 - assp_variance_prev_frame_tex_index_;

        assp_tile_info_prev_frame_tex_index_ = assp_tile_info_curr_frame_tex_index_;
        assp_tile_info_curr_frame_tex_index_ = 1 - assp_tile_info_prev_frame_tex_index_;

        const math::Vec3 modified_important_point = important_point_;

        bbv_grid_updater_.UpdateGrid(modified_important_point);
        // 非選択中のVSPグリッドを進めない。復帰時は現カメラ位置から再構築する。
        if(gi_sample_mode == static_cast<int>(InstantRdvGiSolutionMode::Vsp))
        {
            for(auto& vsp_grid_updater : vsp_grid_updaters_)
            {
                vsp_grid_updater.UpdateGrid(modified_important_point);
            }
        }

        const math::Vec2i hw_depth_size = render_resolution;

        {
            // メンバデフォルト値で初期化し、ランタイム可変値のみ上書き.
            InstantRdvParam param{};

            //Bbv
            {
                param.bbv.grid_resolution = bbv_grid_updater_.Get().resolution.Cast<int>();
                param.bbv.grid_min_pos     = bbv_grid_updater_.Get().min_pos;
                param.bbv.grid_min_voxel_coord = math::Vec3::Floor(bbv_grid_updater_.Get().min_pos * bbv_grid_updater_.Get().cell_size_inv).Cast<int>();

                param.bbv.grid_toroidal_offset =  bbv_grid_updater_.Get().toroidal_offset;
                param.bbv.grid_toroidal_offset_prev =  bbv_grid_updater_.Get().toroidal_offset_prev;

                param.bbv.grid_move_cell_delta = bbv_grid_updater_.Get().min_pos_delta_cell;

                param.bbv.flatten_2d_width = bbv_grid_updater_.Get().flatten_2d_width;

                param.bbv.cell_size       = bbv_grid_updater_.Get().cell_size;
                param.bbv.cell_size_inv    = bbv_grid_updater_.Get().cell_size_inv;

                param.bbv_indirect_cs_thread_group_size = math::Vec3i(0, 0, 0);
                param.bbv_visible_voxel_buffer_size = bbv_fine_update_voxel_count_max_;
                param.bbv_hollow_voxel_buffer_size = bbv_hollow_voxel_list_count_max_;
                param.bbv_occupancy_injection_world_offset = CalcOccupancyInjectionWorldOffsetFromFineCells(
                    InstantRasterDerivedVoxelScene::dbg_bbv_occupancy_injection_fine_cells_,
                    bbv_grid_updater_.Get().cell_size);
            }
            // Vsp
            {
                param.vsp_indirect_cs_thread_group_size = math::Vec3i(pso_vsp_pre_update_->GetThreadGroupSizeX(), pso_vsp_pre_update_->GetThreadGroupSizeY(), pso_vsp_pre_update_->GetThreadGroupSizeZ());
                param.vsp_visible_voxel_buffer_size = vsp_visible_surface_buffer_size_;
                param.vsp_probe_pool_size = static_cast<int>(vsp_probe_pool_size_);
                param.vsp_active_probe_buffer_size = static_cast<int>(vsp_probe_pool_size_);
                param.vsp_lighting_interpolation_enable = InstantRasterDerivedVoxelScene::dbg_vsp_lighting_interpolation_enable_;
                param.vsp_irradiance_volume_propagate_active_probe_weight_enable =
                    InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_propagate_active_probe_weight_enable_;
                param.vsp_irradiance_volume_propagate_active_probe_weight_scale = std::clamp(
                    InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_propagate_active_probe_weight_scale_,
                    1.0f,
                    10.0f);
                param.vsp_probe_lifecycle_enable = InstantRasterDerivedVoxelScene::dbg_vsp_probe_lifecycle_enable_;
                param.vsp_warm_start_enable = InstantRasterDerivedVoxelScene::dbg_vsp_warm_start_enable_;
                param.vsp_relocation_offset_scale_for_cascade_cell_size = InstantRasterDerivedVoxelScene::dbg_vsp_relocation_offset_scale_for_cascade_cell_size_;
                param.vsp_cascade_count = static_cast<int>(vsp_cascade_count_);
                param.vsp_total_cell_count = static_cast<int>(vsp_total_cell_count_);
                param.vsp_probe_atlas_tile_width = static_cast<int>(vsp_probe_atlas_tile_width_);
                param.vsp_probe_atlas_tile_height = static_cast<int>(vsp_probe_atlas_tile_height_);
                const auto& vsp_resolution =
                    vsp_grid_updaters_[0].Get().resolution;
                const u32 vsp_surface_mask_brick_axis =
                    (vsp_resolution.x +
                     k_vsp_surface_mask_brick_resolution - 1u) /
                    k_vsp_surface_mask_brick_resolution;
                param.vsp_surface_mask_brick_axis =
                    static_cast<int>(vsp_surface_mask_brick_axis);
                param.vsp_surface_mask_words_per_cascade =
                    static_cast<int>(
                        vsp_surface_mask_word_count_ /
                        std::max<u32>(vsp_cascade_count_, 1u));
                param.vsp_surface_mask_word_count =
                    static_cast<int>(vsp_surface_mask_word_count_);
                param.main_view_reduced_surface_enable =
                    InstantRasterDerivedVoxelScene::
                        dbg_main_view_reduced_surface_enable_
                        ? 1
                        : 0;
                for(u32 cascade_index = 0; cascade_index < vsp_cascade_count_; ++cascade_index)
                {
                    const auto& cascade_grid = vsp_grid_updaters_[cascade_index].Get();
                    auto& cascade_param = param.vsp_cascade[cascade_index];
                    cascade_param.grid.grid_resolution = cascade_grid.resolution.Cast<int>();
                    cascade_param.grid.grid_min_pos = cascade_grid.min_pos;
                    cascade_param.grid.grid_min_voxel_coord = math::Vec3::Floor(cascade_grid.min_pos * cascade_grid.cell_size_inv).Cast<int>();
                    cascade_param.grid.grid_toroidal_offset = cascade_grid.toroidal_offset;
                    cascade_param.grid.grid_toroidal_offset_prev = cascade_grid.toroidal_offset_prev;
                    cascade_param.grid.grid_move_cell_delta = cascade_grid.min_pos_delta_cell;
                    cascade_param.grid.flatten_2d_width = cascade_grid.flatten_2d_width;
                    cascade_param.grid.cell_size = cascade_grid.cell_size;
                    cascade_param.grid.cell_size_inv = cascade_grid.cell_size_inv;
                    cascade_param.cell_offset = vsp_cascade_cell_offset_array_[cascade_index];
                    cascade_param.cell_count = cascade_grid.total_count;
                    cascade_param.irradiance_volume_texture_z_offset =
                        cascade_index *
                        (cascade_grid.resolution.z + k_vsp_irradiance_volume_guard_texel_count) *
                        k_vsp_irradiance_volume_sh_texture_count;
                }
            }

            param.tex_main_view_depth_size = hw_depth_size;
            param.frame_count = frame_count_;

            // dbg_系: ランタイム変更可能なパラメータ.
            param.ss_probe_temporal_filter_normal_cos_threshold = k_default_instant_rdv_param.ss_probe_temporal_filter_normal_cos_threshold;
            param.ss_probe_temporal_filter_plane_dist_threshold = k_default_instant_rdv_param.ss_probe_temporal_filter_plane_dist_threshold;

            param.main_light_dir_ws = main_light_dir;

            param.debug_view_category = InstantRasterDerivedVoxelScene::dbg_view_category_;
            param.debug_view_sub_mode = InstantRasterDerivedVoxelScene::dbg_view_sub_mode_;
            param.debug_vsp_irradiance_volume_slice_scale =
                InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scale_;
            param.debug_vsp_irradiance_volume_slice_scroll_x =
                InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scroll_x_;
            param.debug_vsp_irradiance_volume_slice_scroll_y =
                InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_slice_scroll_y_;
            param.debug_vsp_shading_test_signal = std::clamp(
                InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_signal_, 0, 3);
            param.debug_vsp_shading_test_cascade = std::clamp(
                InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_cascade_,
                -1,
                static_cast<int>(vsp_cascade_count_) - 1);
            param.debug_vsp_shading_test_trilinear_enable =
                InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_trilinear_enable_ ? 1 : 0;
            param.debug_vsp_shading_test_cascade_interpolation_enable =
                InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_cascade_interpolation_enable_ ? 1 : 0;
            param.debug_vsp_shading_test_irradiance_ev = std::clamp(
                InstantRasterDerivedVoxelScene::dbg_vsp_shading_test_irradiance_ev_,
                -16.0f,
                16.0f);
            param.debug_bbv_probe_mode = InstantRasterDerivedVoxelScene::dbg_bbv_probe_debug_mode_;
            param.debug_bbv_depth_test_enable = InstantRasterDerivedVoxelScene::dbg_bbv_depth_test_enable_;
            param.debug_vsp_probe_mode = InstantRasterDerivedVoxelScene::dbg_vsp_probe_debug_mode_;
            param.debug_vsp_irradiance_volume_mode = InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_debug_mode_;
            param.debug_vsp_probe_use_relocated_pos = InstantRasterDerivedVoxelScene::dbg_vsp_probe_use_relocated_pos_;
            param.debug_vsp_update_ray_jitter_enable = InstantRasterDerivedVoxelScene::dbg_vsp_update_ray_jitter_enable_;
            param.debug_vsp_probe_cascade = InstantRasterDerivedVoxelScene::dbg_vsp_probe_debug_cascade_;

            param.debug_probe_radius = InstantRasterDerivedVoxelScene::dbg_probe_scale_ * 0.5f * bbv_grid_updater_.Get().cell_size * static_cast<float>(k_bbv_per_voxel_resolution_inv);
            param.debug_probe_near_geom_scale = InstantRasterDerivedVoxelScene::dbg_probe_near_geom_scale_;
            param.assp_spatial_filter_enable = InstantRasterDerivedVoxelScene::assp_spatial_filter_enable_;
            param.assp_spatial_filter_normal_cos_threshold = InstantRasterDerivedVoxelScene::assp_spatial_filter_normal_cos_threshold_;
            param.assp_spatial_filter_depth_exp_scale = InstantRasterDerivedVoxelScene::assp_spatial_filter_depth_exp_scale_;
            param.assp_temporal_reprojection_enable = InstantRasterDerivedVoxelScene::assp_temporal_reprojection_enable_;
            param.assp_ray_guiding_enable = InstantRasterDerivedVoxelScene::assp_ray_guiding_enable_;
            param.assp_ray_budget_min_rays = InstantRasterDerivedVoxelScene::assp_ray_budget_min_rays_;
            param.assp_ray_budget_max_rays = InstantRasterDerivedVoxelScene::assp_ray_budget_max_rays_;
            param.assp_ray_budget_variance_weight = InstantRasterDerivedVoxelScene::assp_ray_budget_variance_weight_;
            param.assp_ray_budget_normal_delta_weight = InstantRasterDerivedVoxelScene::assp_ray_budget_normal_delta_weight_;
            param.assp_ray_budget_depth_delta_weight = InstantRasterDerivedVoxelScene::assp_ray_budget_depth_delta_weight_;
            param.assp_ray_budget_no_history_bias = InstantRasterDerivedVoxelScene::assp_ray_budget_no_history_bias_;
            param.assp_ray_budget_scale = InstantRasterDerivedVoxelScene::assp_ray_budget_scale_;
            param.assp_debug_freeze_frame_random_enable = InstantRasterDerivedVoxelScene::assp_debug_freeze_frame_random_enable_;

            dispatch_param_cache_ = param;
        }

        dispatch_requires_initial_clear_ = is_first_dispatch;
        // VSP復帰時は時間的状態を初期化し、非選択中の古いActiveProbeを参照しない。
        dispatch_requires_vsp_clear_ =
            is_first_dispatch ||
            (is_gi_sample_mode_changed &&
             gi_sample_mode == static_cast<int>(InstantRdvGiSolutionMode::Vsp));
        return true;
    }

    void BitmaskBrickVoxelGi::UploadFrameConstants(rhi::DeviceDep* p_device)
    {
        cbh_dispatch_ = p_device->GetConstantBufferPool()->Alloc(sizeof(InstantRdvParam));
        auto* p_mapped = cbh_dispatch_->buffer.MapAs<InstantRdvParam>();
        std::memcpy(p_mapped, &dispatch_param_cache_, sizeof(InstantRdvParam));
        cbh_dispatch_->buffer.Unmap();
    }

    void BitmaskBrickVoxelGi::Dispatch_Begin(
        rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Dispatch_Begin");

        // 初回クリア.
        if (dispatch_requires_initial_clear_)
        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvInitClear");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_clear_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
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
        }
        if(dispatch_requires_vsp_clear_)
        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspInitClear");

                vsp_cell_probe_index_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_probe_pool_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_probe_free_stack_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_active_probe_list_[0].ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_active_probe_list_[1].ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_visible_surface_list_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_clear_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_clear_->SetView(&desc_set, "RWVspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.uav.Get());
                pso_vsp_clear_->SetView(&desc_set, "RWVspProbePoolBuffer", vsp_probe_pool_buffer_.uav.Get());
                pso_vsp_clear_->SetView(&desc_set, "RWVspProbeFreeStack", vsp_probe_free_stack_buffer_.uav.Get());
                pso_vsp_clear_->SetView(&desc_set, "RWVspActiveProbeListPrev", vsp_active_probe_list_[0].uav.Get());
                pso_vsp_clear_->SetView(&desc_set, "RWVspActiveProbeListCurr", vsp_active_probe_list_[1].uav.Get());
                pso_vsp_clear_->SetView(&desc_set, "RWSurfaceProbeCellList", vsp_visible_surface_list_.uav.Get());
                pso_vsp_clear_->SetView(&desc_set, k_shader_bind_name_vsp_atlas_uav.Get(), vsp_probe_atlas_tex_.uav.Get());
                pso_vsp_clear_->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_uav.Get(), vsp_irradiance_volume_sh_texture_.uav.Get());
                vsp_irradiance_volume_sh_texture_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                vsp_probe_atlas_tex_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                p_command_list->SetPipelineState(pso_vsp_clear_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_clear_.Get(), &desc_set);
                pso_vsp_clear_->DispatchHelper(p_command_list, std::max<u32>(vsp_total_cell_count_, vsp_probe_pool_size_ + 1), 1, 1);

                p_command_list->ResourceUavBarrier(vsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_active_probe_list_[0].buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_active_probe_list_[1].buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_visible_surface_list_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_irradiance_volume_sh_texture_.texture.Get());
            }
            dispatch_requires_vsp_clear_ = false;
        }
        if(dispatch_requires_initial_clear_)
        {
            {
                // ASSPリソースは作成直後のCommonから、最初のUAV使用前に一度だけ遷移する。
                for(int i = 0; i < 2; ++i)
                {
                    p_command_list->ResourceBarrier(
                        assp_probe_tex_[i].texture.Get(),
                        rhi::EResourceState::Common,
                        rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(
                        assp_probe_variance_tex_[i].texture.Get(),
                        rhi::EResourceState::Common,
                        rhi::EResourceState::UnorderedAccess);
                    p_command_list->ResourceBarrier(
                        assp_probe_tile_info_tex_[i].texture.Get(),
                        rhi::EResourceState::Common,
                        rhi::EResourceState::UnorderedAccess);
                }
                p_command_list->ResourceBarrier(
                    assp_probe_packed_sh_tex_.texture.Get(),
                    rhi::EResourceState::Common,
                    rhi::EResourceState::UnorderedAccess);
                p_command_list->ResourceBarrier(
                    assp_probe_best_prev_tile_tex_.texture.Get(),
                    rhi::EResourceState::Common,
                    rhi::EResourceState::UnorderedAccess);

                p_command_list->SetPipelineState(pso_assp_probe_clear_.Get());
                for(int i = 0; i < 2; ++i)
                {
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_assp_probe_clear_->SetView(&desc_set, k_shader_bind_name_asspprobe_uav.Get(), assp_probe_tex_[i].uav.Get());
                    pso_assp_probe_clear_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_uav.Get(), assp_probe_tile_info_tex_[i].uav.Get());
                    p_command_list->SetDescriptorSet(pso_assp_probe_clear_.Get(), &desc_set);
                    pso_assp_probe_clear_->DispatchHelper(p_command_list, assp_probe_tex_[i].texture->GetWidth(), assp_probe_tex_[i].texture->GetHeight(), 1);
                }

            }
        }
        dispatch_requires_initial_clear_ = false;
        // Bbv Begin Update Pass.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_begin_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
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
                        const ngl::render::task::RenderPassViewInfo& main_view_info,
            
                        const InjectionSourceDepthBufferInfo& depth_buffer_info
    )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Dispatch_Bbv_OccupancyUpdate_View");

        // VSP SurfaceCellのフレーム内重複排除マスクを消去する。
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspSurfaceMaskClear");

            vsp_surface_cell_mask_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_vsp_surface_mask_clear_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_vsp_surface_mask_clear_->SetView(&desc_set, "RWVspSurfaceCellMaskBuffer", vsp_surface_cell_mask_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_vsp_surface_mask_clear_.Get());
            p_command_list->SetDescriptorSet(pso_vsp_surface_mask_clear_.Get(), &desc_set);
            pso_vsp_surface_mask_clear_->DispatchHelper(
                p_command_list,
                vsp_surface_mask_word_count_,
                1,
                1);
            p_command_list->ResourceUavBarrier(vsp_surface_cell_mask_buffer_.buffer.Get());
        }

        const bool use_reduced_surface =
            InstantRasterDerivedVoxelScene::dbg_main_view_reduced_surface_enable_;

        auto func_call_bbv_removal_frustum_pass = [this](
            rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle cbh_injection_view_info
        )
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRemovalFrustumCull");

            ngl::rhi::DescriptorSetDep desc_set = {};
            auto* pso_bbv_removal_frustum_cull = pso_bbv_removal_frustum_cull_.Get();
            pso_bbv_removal_frustum_cull->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_bbv_removal_frustum_cull->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
            pso_bbv_removal_frustum_cull->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
            pso_bbv_removal_frustum_cull->SetView(&desc_set, "RWFrustumBrickList", bbv_removal_frustum_brick_list_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_removal_frustum_cull);
            p_command_list->SetDescriptorSet(pso_bbv_removal_frustum_cull, &desc_set);
            pso_bbv_removal_frustum_cull->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);
            p_command_list->ResourceUavBarrier(bbv_removal_frustum_brick_list_.buffer.Get());
        };
        auto func_call_bbv_removal_carving_indirect_arg_build_pass = [this](
            rhi::GraphicsCommandListDep* p_command_list
        )
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRemovalCarvingIndirectArgBuild");

            bbv_removal_frustum_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_removal_carving_indirect_arg_build_->SetView(&desc_set, "FrustumBrickList", bbv_removal_frustum_brick_list_.srv.Get());
            pso_bbv_removal_carving_indirect_arg_build_->SetView(&desc_set, "RWFrustumBrickIndirectArg", bbv_removal_frustum_indirect_arg_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_removal_carving_indirect_arg_build_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_removal_carving_indirect_arg_build_.Get(), &desc_set);
            pso_bbv_removal_carving_indirect_arg_build_->DispatchHelper(p_command_list, 1, 1, 1);

            bbv_removal_frustum_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
        };
        auto func_call_bbv_occupancy_injection_pass = [this](
            rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle cbh_injection_view_info,
            const InjectionSourceDepthBufferViewInfo& target_depth_info,
            bool integrate_surface_detection,
            bool use_reduced_surface
        )
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(
                p_command_list,
                use_reduced_surface
                    ? "BbvOccupancyInjection_ReducedSurface"
                    : integrate_surface_detection
                    ? "BbvOccupancyInjection_IntegratedSurface"
                    : "BbvOccupancyInjection");

            auto* pso = use_reduced_surface
                ? pso_bbv_occupancy_injection_apply_reduced_surface_.Get()
                : integrate_surface_detection
                    ? pso_bbv_occupancy_injection_apply_integrated_surface_.Get()
                    : pso_bbv_occupancy_injection_apply_.Get();
            ngl::rhi::DescriptorSetDep desc_set = {};
            if(use_reduced_surface)
            {
                pso->SetView(
                    &desc_set,
                    "TexReducedSurfaceBuffer",
                    reduced_surface_buffer_tex_.srv.Get());
            }
            else
            {
                pso->SetView(
                    &desc_set,
                    "TexHardwareDepth",
                    target_depth_info.hw_depth_srv.Get());
            }
            pso->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
            pso->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
            if(integrate_surface_detection && !use_reduced_surface)
            {
                pso->SetView(&desc_set, "RWVspSurfaceCellMaskBuffer", vsp_surface_cell_mask_buffer_.uav.Get());
                pso->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());
            }

            p_command_list->SetPipelineState(pso);
            p_command_list->SetDescriptorSet(pso, &desc_set);
            const u32 dispatch_width = use_reduced_surface
                ? reduced_surface_buffer_tex_.texture->GetWidth()
                : static_cast<u32>(target_depth_info.atlas_resolution.x);
            const u32 dispatch_height = use_reduced_surface
                ? reduced_surface_buffer_tex_.texture->GetHeight()
                : static_cast<u32>(target_depth_info.atlas_resolution.y);
            pso->DispatchHelper(
                p_command_list,
                dispatch_width,
                dispatch_height,
                1);
            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
            if(integrate_surface_detection && !use_reduced_surface)
            {
                p_command_list->ResourceUavBarrier(vsp_surface_cell_mask_buffer_.buffer.Get());
            }
        };
        auto func_call_bbv_removal_carving_pass = [this](
            rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle cbh_injection_view_info,
            const InjectionSourceDepthBufferViewInfo& target_depth_info
        )
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRemovalCarving");

            ngl::rhi::DescriptorSetDep desc_set = {};
            auto* pso = pso_bbv_removal_carving_.Get();
            pso->SetView(&desc_set, "TexHardwareDepth", target_depth_info.hw_depth_srv.Get());
            pso->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
            pso->SetView(&desc_set, "FrustumBrickList", bbv_removal_frustum_brick_list_.srv.Get());
            pso->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());
            p_command_list->SetPipelineState(pso);
            p_command_list->SetDescriptorSet(pso, &desc_set);
            p_command_list->DispatchIndirect(bbv_removal_frustum_indirect_arg_.buffer.Get());
            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
        };

        // viewごとにDepthBufferのInjection / Removal Passを実行.
        const int num_depth_buffer = 1 + static_cast<int>(depth_buffer_info.sub_array.size());
        for(int i = 0; i < num_depth_buffer; ++i)
        {
            // SubViewを先に処理し、MainViewを最後に処理する。
            // 最終的なBBV状態でMainViewの可視表面Occupancyを優先するため、
            // MainViewのInjectionとRemovalをフレーム内の最後のView更新として実行する。
            const bool is_main_view_update = (i == (num_depth_buffer - 1));
            const InjectionSourceDepthBufferViewInfo& target_depth_info =
                is_main_view_update ? depth_buffer_info.primary : depth_buffer_info.sub_array[i];
            
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
                    const float near_plane_depth = (target_depth_info.proj_mat.m[2][3] > 0.0f) ? 1.0f : 0.0f;
                    p->cb_near_plane_view_z = calc_view_z_from_ndc_z(near_plane_depth, p->cb_ndc_z_to_view_z_coef);
                    // ViewDepthBufferの他, ShadowMapによるInjectionもしたいのでShadowMapAtlas用にオフセット考慮.
                    p->cb_view_depth_buffer_offset_size = math::Vec4i(
                        target_depth_info.atlas_offset.x,
                        target_depth_info.atlas_offset.y,
                        target_depth_info.atlas_resolution.x,
                        target_depth_info.atlas_resolution.y
                    );
                    p->cb_is_main_view = is_main_view_update ? 1 : 0;
                    p->cb_padding0 = math::Vec2i(0, 0);
                    const auto frustum_planes = CalcBbvFrustumPlanes(target_depth_info.view_mat, target_depth_info.proj_mat);
                    for (size_t plane_index = 0; plane_index < frustum_planes.size(); ++plane_index)
                    {
                        p->cb_frustum_planes[plane_index] = frustum_planes[plane_index];
                    }
                }
                cbh_injection_view_info->buffer.Unmap();
            }

            const bool use_reduced_surface_for_view =
                use_reduced_surface && is_main_view_update;
            if(use_reduced_surface_for_view)
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(
                    p_command_list,
                    "MainViewReducedSurfaceBufferBuild");

                reduced_surface_buffer_tex_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_reduced_surface_buffer_build_->SetView(
                    &desc_set,
                    "cb_instant_rdv",
                    &cbh_dispatch_->cbv);
                pso_reduced_surface_buffer_build_->SetView(
                    &desc_set,
                    "cb_injection_src_view_info",
                    &cbh_injection_view_info->cbv);
                pso_reduced_surface_buffer_build_->SetView(
                    &desc_set,
                    "TexHardwareDepth",
                    target_depth_info.hw_depth_srv.Get());
                pso_reduced_surface_buffer_build_->SetView(
                    &desc_set,
                    "RWReducedSurfaceBuffer",
                    reduced_surface_buffer_tex_.uav.Get());

                p_command_list->SetPipelineState(
                    pso_reduced_surface_buffer_build_.Get());
                p_command_list->SetDescriptorSet(
                    pso_reduced_surface_buffer_build_.Get(),
                    &desc_set);
                pso_reduced_surface_buffer_build_->DispatchHelper(
                    p_command_list,
                    reduced_surface_buffer_tex_.texture->GetWidth(),
                    reduced_surface_buffer_tex_.texture->GetHeight(),
                    1);
                p_command_list->ResourceUavBarrier(
                    reduced_surface_buffer_tex_.texture.Get());
                reduced_surface_buffer_tex_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
            }

            // フレーム番号をサフィックスにしたラベルでマーカー発行.
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, (std::string("BbvPerViewProcess_") + std::to_string(i)).c_str());
            
            // Bbv Begin View Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBeginViewUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_begin_view_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_bbv_begin_view_update_->SetView(&desc_set, "RWFrustumBrickList", bbv_removal_frustum_brick_list_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_begin_view_update_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_begin_view_update_.Get(), &desc_set);
                pso_bbv_begin_view_update_->DispatchHelper(p_command_list, 1, 1, 1);

                p_command_list->ResourceUavBarrier(bbv_removal_frustum_brick_list_.buffer.Get());
            }


            // Depth Removal flow は Injection 後の最新bitmaskで候補抽出してからCarvingする。
            // これにより Frustum ActiveList には Empty Brick を含めず、Carving 起動数を最小化できる。
            if(target_depth_info.is_enable_injection_pass)
            {
                // Legacy MainView/ShadowView injection path is intentionally unchanged.
                func_call_bbv_occupancy_injection_pass(
                    p_command_list,
                    cbh_injection_view_info,
                    target_depth_info,
                    i == (num_depth_buffer - 1),
                    use_reduced_surface_for_view);
            }
            if(target_depth_info.is_enable_removal_pass)
            {
                func_call_bbv_removal_frustum_pass(p_command_list, cbh_injection_view_info);
                func_call_bbv_removal_carving_indirect_arg_build_pass(p_command_list);
                func_call_bbv_removal_carving_pass(
                    p_command_list,
                    cbh_injection_view_info,
                    target_depth_info);
            }
        }

        // BBV count rebuild pass.
        // Injection / Removal は bitmask のみを更新し、Brick count はここで再構築する。
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvBrickCountAggregate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_brick_count_aggregate_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_bbv_brick_count_aggregate_->SetView(&desc_set, "RWBitmaskBrickVoxel", bbv_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_brick_count_aggregate_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_brick_count_aggregate_.Get(), &desc_set);
            pso_bbv_brick_count_aggregate_->DispatchHelper(p_command_list, bbv_grid_updater_.Get().total_count, 1, 1);

            p_command_list->ResourceUavBarrier(bbv_buffer_.buffer.Get());
        }

    }

    void BitmaskBrickVoxelGi::Dispatch_Bbv_RadianceInjection_View(rhi::GraphicsCommandListDep* p_command_list,
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
            const float near_plane_depth = (view_info.proj_mat.m[2][3] > 0.0f) ? 1.0f : 0.0f;
            p->cb_near_plane_view_z = calc_view_z_from_ndc_z(near_plane_depth, p->cb_ndc_z_to_view_z_coef);
            p->cb_view_depth_buffer_offset_size = math::Vec4i(
                view_info.atlas_offset.x,
                view_info.atlas_offset.y,
                view_info.atlas_resolution.x,
                view_info.atlas_resolution.y);
            p->cb_is_main_view = 1;
            p->cb_padding0 = math::Vec2i(0, 0);
            const auto frustum_planes = CalcBbvFrustumPlanes(view_info.view_mat, view_info.proj_mat);
            for (size_t plane_index = 0; plane_index < frustum_planes.size(); ++plane_index)
            {
                p->cb_frustum_planes[plane_index] = frustum_planes[plane_index];
            }
            cbh_injection_view_info->buffer.Unmap();
        }

        if(k_bbv_radiance_injection_frame_skip_count == 0 ||
           (frame_count_ %
            (k_bbv_radiance_injection_frame_skip_count + 1)) == 0)
        {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(
                    p_command_list,
                    "BbvRadianceInjection");
                const bool use_reduced_surface =
                    InstantRasterDerivedVoxelScene::dbg_main_view_reduced_surface_enable_;
                const auto injection_dispatch_resolution = use_reduced_surface
                    ? math::Vec2u(
                        reduced_surface_buffer_tex_.texture->GetWidth(),
                        reduced_surface_buffer_tex_.texture->GetHeight())
                    : CalcBbvRadianceInjectionDispatchResolution(math::Vec2u(
                        static_cast<u32>(view_info.atlas_resolution.x),
                        static_cast<u32>(view_info.atlas_resolution.y)));
                auto& pso_bbv_radiance_injection = use_reduced_surface
                    ? pso_bbv_radiance_injection_apply_reduced_surface_
                    : pso_bbv_radiance_injection_apply_short_ray_;

                ngl::rhi::DescriptorSetDep desc_set = {};
                if(use_reduced_surface)
                {
                    pso_bbv_radiance_injection->SetView(
                        &desc_set,
                        "TexReducedSurfaceBuffer",
                        reduced_surface_buffer_tex_.srv.Get());
                }
                else
                {
                    pso_bbv_radiance_injection->SetView(
                        &desc_set,
                        "TexHardwareDepth",
                        view_info.hw_depth_srv.Get());
                }
                pso_bbv_radiance_injection->SetView(&desc_set, "TexInputRadiance", view_info.hw_color_srv.Get());
                pso_bbv_radiance_injection->SetView(&desc_set, "cb_injection_src_view_info", &cbh_injection_view_info->cbv);
                pso_bbv_radiance_injection->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_bbv_radiance_injection->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());
                pso_bbv_radiance_injection->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());

                p_command_list->SetPipelineState(pso_bbv_radiance_injection.Get());
                p_command_list->SetDescriptorSet(pso_bbv_radiance_injection.Get(), &desc_set);
                pso_bbv_radiance_injection->DispatchHelper(p_command_list, injection_dispatch_resolution.x, injection_dispatch_resolution.y, 1);

                p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvRadianceResolve");
                const auto resolve_dispatch_count = CalcBbvRadianceResolveDispatchCount(bbv_grid_updater_.Get().resolution);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_bbv_radiance_resolve_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_bbv_radiance_resolve_->SetView(&desc_set, "RWBbvRadianceAccumBuffer", bbv_radiance_accum_buffer_.uav.Get());
                pso_bbv_radiance_resolve_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_bbv_radiance_resolve_.Get());
                p_command_list->SetDescriptorSet(pso_bbv_radiance_resolve_.Get(), &desc_set);
                pso_bbv_radiance_resolve_->DispatchHelper(p_command_list, resolve_dispatch_count, 1, 1);

                p_command_list->ResourceUavBarrier(bbv_radiance_accum_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
            }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_Bbv_Main(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Dispatch_Bbv_Main");


        // Voxel Update.
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvCommonUpdate");

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_element_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_bbv_element_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_bbv_element_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_element_update_->SetView(&desc_set, "RWBitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_bbv_element_update_.Get());
            p_command_list->SetDescriptorSet(pso_bbv_element_update_.Get(), &desc_set);
            pso_bbv_element_update_->DispatchHelper(p_command_list, (bbv_grid_updater_.Get().total_count + (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT)) / (BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT+1), 1, 1);

            p_command_list->ResourceUavBarrier(bbv_optional_data_buffer_.buffer.Get());
        }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_AsspProbe(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Dispatch_AsspProbe");

        const ngl::u32 assp_probe_update_write_index = assp_curr_frame_tex_index_;
        const ngl::u32 assp_probe_tile_info_curr_index = assp_tile_info_curr_frame_tex_index_;
        const ngl::u32 assp_probe_history_index = assp_prev_frame_tex_index_;
        const ngl::u32 assp_probe_tile_info_history_index = assp_tile_info_prev_frame_tex_index_;
        const ngl::u32 assp_probe_variance_write_index = assp_variance_curr_frame_tex_index_;
        const ngl::u32 assp_probe_variance_history_index = assp_variance_prev_frame_tex_index_;
        const bool is_assp_spatial_filter_enable = (0 != InstantRasterDerivedVoxelScene::assp_spatial_filter_enable_);
        const u32 assp_probe_tile_count =
            static_cast<u32>(assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture->GetWidth()) *
            static_cast<u32>(assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].texture->GetHeight());
        const u32 assp_probe_thread_count_build_ray_meta = (assp_probe_tile_count > 0u) ? assp_probe_tile_count : 1u;
        const u32 assp_probe_thread_count_update_like = (assp_probe_tile_count > 0u)
            ? (assp_probe_tile_count * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT)
            : 1u;

        {
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeBegin");

                assp_probe_total_ray_count_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_begin_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_uav.Get(), assp_probe_total_ray_count_buffer_.uav.Get());
                p_command_list->SetPipelineState(pso_assp_probe_begin_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_begin_.Get(), &desc_set);
                pso_assp_probe_begin_->DispatchHelper(p_command_list, 1, 1, 1);

                p_command_list->ResourceUavBarrier(assp_probe_total_ray_count_buffer_.buffer.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbePreUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_preupdate_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
                pso_assp_probe_preupdate_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_assp_probe_preupdate_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
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
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeBuildRayMeta");

                assp_probe_ray_meta_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                assp_probe_ray_query_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_history_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_variance_srv.Get(), assp_probe_variance_tex_[assp_probe_variance_history_index].srv.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_uav.Get(), assp_probe_ray_meta_buffer_.uav.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_query_uav.Get(), assp_probe_ray_query_buffer_.uav.Get());
                pso_assp_probe_build_ray_meta_->SetView(&desc_set, k_shader_bind_name_assp_probe_total_ray_count_uav.Get(), assp_probe_total_ray_count_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_build_ray_meta_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_build_ray_meta_.Get(), &desc_set);
                pso_assp_probe_build_ray_meta_->DispatchHelper(p_command_list, assp_probe_thread_count_build_ray_meta, 1, 1);

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
                pso_assp_probe_trace_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
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
                pso_assp_probe_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_srv.Get(), assp_probe_tex_[assp_probe_history_index].srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_srv.Get(), assp_probe_ray_meta_buffer_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_result_srv.Get(), assp_probe_ray_result_buffer_.srv.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_uav.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].uav.Get());
                pso_assp_probe_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_uav.Get(), assp_probe_tex_[assp_probe_update_write_index].uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_update_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_update_.Get(), &desc_set);
                pso_assp_probe_update_->DispatchHelper(p_command_list, assp_probe_thread_count_update_like, 1, 1);

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
                pso_assp_probe_spatial_filter_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
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
                pso_assp_probe_variance_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                // LOD split/merge は spatial filter 後ではなく、生の update 出力に対する分散を見て判定する。
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_probe_update_write_index].srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_history_variance_srv.Get(), assp_probe_variance_tex_[assp_probe_variance_history_index].srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_best_prev_tile_srv.Get(), assp_probe_best_prev_tile_tex_.srv.Get());
                pso_assp_probe_variance_->SetView(&desc_set, k_shader_bind_name_asspprobe_variance_uav.Get(), assp_probe_variance_tex_[assp_probe_variance_write_index].uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_variance_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_variance_.Get(), &desc_set);
                pso_assp_probe_variance_->DispatchHelper(p_command_list, assp_probe_thread_count_update_like, 1, 1);

                p_command_list->ResourceUavBarrier(assp_probe_variance_tex_[assp_probe_variance_write_index].texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "AdaptiveScreenSpaceProbeShUpdate");

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_assp_probe_sh_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv.Get());
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_probe_tile_info_curr_index].srv.Get());
                pso_assp_probe_sh_update_->SetView(&desc_set, k_shader_bind_name_asspprobe_packed_sh_uav.Get(), assp_probe_packed_sh_tex_.uav.Get());

                p_command_list->SetPipelineState(pso_assp_probe_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_assp_probe_sh_update_.Get(), &desc_set);
                pso_assp_probe_sh_update_->DispatchHelper(p_command_list, assp_probe_thread_count_update_like, 1, 1);

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

    void BitmaskBrickVoxelGi::Dispatch_Vsp(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
                        )
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Dispatch_Vsp");

        const u32 vsp_active_probe_curr_list_index = frame_count_ & 1u;
        const u32 vsp_active_probe_prev_list_index = 1u - vsp_active_probe_curr_list_index;
        auto& vsp_active_probe_curr_list = vsp_active_probe_list_[vsp_active_probe_curr_list_index];
        auto& vsp_active_probe_prev_list = vsp_active_probe_list_[vsp_active_probe_prev_list_index];
        vsp_active_probe_prev_list.ResourceBarrier(
            p_command_list,
            rhi::EResourceState::ShaderRead);
        vsp_active_probe_curr_list.ResourceBarrier(
            p_command_list,
            rhi::EResourceState::UnorderedAccess);
        // VSP.
        {
            // Vsp Prev Active IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspGeneratePrevActiveIndirectArg");

                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_generate_prev_active_indirect_arg_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_generate_prev_active_indirect_arg_->SetView(&desc_set, "ProbeIndexList", vsp_active_probe_prev_list.srv.Get());
                pso_vsp_generate_prev_active_indirect_arg_->SetView(&desc_set, "RWVspIndirectArg", vsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_generate_prev_active_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_generate_prev_active_indirect_arg_.Get(), &desc_set);
                pso_vsp_generate_prev_active_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Vsp Begin Update Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspBeginUpdate");

                vsp_cell_probe_index_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                vsp_probe_pool_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_begin_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_vsp_begin_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_begin_update_->SetView(&desc_set, "RWVspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, "RWVspProbePoolBuffer", vsp_probe_pool_buffer_.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, "RWVspProbeFreeStack", vsp_probe_free_stack_buffer_.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, "VspActiveProbeListPrev", vsp_active_probe_prev_list.srv.Get());
                pso_vsp_begin_update_->SetView(&desc_set, "RWVspActiveProbeListCurr", vsp_active_probe_curr_list.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, "RWSurfaceProbeCellList", vsp_visible_surface_list_.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_uav.Get(), vsp_irradiance_volume_sh_texture_.uav.Get());
                vsp_irradiance_volume_sh_texture_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                pso_vsp_begin_update_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_request_uav.Get(), vsp_probe_ray_request_buffer_.uav.Get());
                pso_vsp_begin_update_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_result_uav.Get(), vsp_probe_ray_result_buffer_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_begin_update_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_begin_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(vsp_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(vsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_active_probe_curr_list.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_visible_surface_list_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_irradiance_volume_sh_texture_.texture.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_ray_request_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_ray_result_buffer_.buffer.Get());
            }
            
            // Vsp Visible Surface Processing Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspSurfaceMaskProcessing");
                const bool use_reduced_surface =
                    InstantRasterDerivedVoxelScene::
                        dbg_main_view_reduced_surface_enable_;
                if(use_reduced_surface)
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(
                        p_command_list,
                        "VspReducedSurfaceDetection");

                    vsp_surface_cell_mask_buffer_.ResourceBarrier(
                        p_command_list,
                        rhi::EResourceState::UnorderedAccess);
                    vsp_visible_surface_list_.ResourceBarrier(
                        p_command_list,
                        rhi::EResourceState::UnorderedAccess);
                    vsp_visible_surface_source_texel_list_.ResourceBarrier(
                        p_command_list,
                        rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "cb_ngl_sceneview",
                        &scene_cbv->cbv);
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "cb_instant_rdv",
                        &cbh_dispatch_->cbv);
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "TexReducedSurfaceBuffer",
                        reduced_surface_buffer_tex_.srv.Get());
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "RWVspSurfaceCellMaskBuffer",
                        vsp_surface_cell_mask_buffer_.uav.Get());
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "RWSurfaceProbeCellList",
                        vsp_visible_surface_list_.uav.Get());
                    pso_vsp_surface_detect_reduced_->SetView(
                        &desc_set,
                        "RWSurfaceProbeSourceTexelList",
                        vsp_visible_surface_source_texel_list_.uav.Get());

                    p_command_list->SetPipelineState(
                        pso_vsp_surface_detect_reduced_.Get());
                    p_command_list->SetDescriptorSet(
                        pso_vsp_surface_detect_reduced_.Get(),
                        &desc_set);
                    pso_vsp_surface_detect_reduced_->DispatchHelper(
                        p_command_list,
                        reduced_surface_buffer_tex_.texture->GetWidth(),
                        reduced_surface_buffer_tex_.texture->GetHeight(),
                        1);

                    p_command_list->ResourceUavBarrier(
                        vsp_surface_cell_mask_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(
                        vsp_visible_surface_list_.buffer.Get());
                    p_command_list->ResourceUavBarrier(
                        vsp_visible_surface_source_texel_list_.buffer.Get());
                }
                else
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(
                        p_command_list,
                        "VspSurfaceMaskCompact");

                    vsp_surface_cell_mask_buffer_.ResourceBarrier(
                        p_command_list,
                        rhi::EResourceState::ShaderRead);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_surface_mask_compact_->SetView(
                        &desc_set,
                        "cb_instant_rdv",
                        &cbh_dispatch_->cbv);
                    pso_vsp_surface_mask_compact_->SetView(
                        &desc_set,
                        "VspSurfaceCellMaskBuffer",
                        vsp_surface_cell_mask_buffer_.srv.Get());
                    pso_vsp_surface_mask_compact_->SetView(
                        &desc_set,
                        "RWSurfaceProbeCellList",
                        vsp_visible_surface_list_.uav.Get());

                    p_command_list->SetPipelineState(
                        pso_vsp_surface_mask_compact_.Get());
                    p_command_list->SetDescriptorSet(
                        pso_vsp_surface_mask_compact_.Get(),
                        &desc_set);
                    pso_vsp_surface_mask_compact_->DispatchHelper(
                        p_command_list,
                        vsp_surface_mask_word_count_,
                        1,
                        1);

                    p_command_list->ResourceUavBarrier(
                        vsp_visible_surface_list_.buffer.Get());
                }
                vsp_visible_surface_list_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
            }
            // Vsp IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspGenerateIndirectArg");
                 
                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_generate_indirect_arg_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_generate_indirect_arg_->SetView(
                    &desc_set,
                    "ProbeIndexList",
                    vsp_visible_surface_list_.srv.Get());
                pso_vsp_generate_indirect_arg_->SetView(&desc_set, "RWVspIndirectArg", vsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_generate_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_generate_indirect_arg_.Get(), &desc_set);
                pso_vsp_generate_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            // Vsp PreUpdate Pass.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspPreUpdate");

                vsp_probe_atlas_tex_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_pre_update_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                pso_vsp_pre_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_pre_update_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_vsp_pre_update_->SetView(
                    &desc_set,
                    "TexReducedSurfaceBuffer",
                    reduced_surface_buffer_tex_.srv.Get());

                pso_vsp_pre_update_->SetView(
                    &desc_set,
                    "SurfaceProbeCellList",
                    vsp_visible_surface_list_.srv.Get());
                vsp_visible_surface_source_texel_list_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
                pso_vsp_pre_update_->SetView(
                    &desc_set,
                    "SurfaceProbeSourceTexelList",
                    vsp_visible_surface_source_texel_list_.srv.Get());
                pso_vsp_pre_update_->SetView(&desc_set, "RWVspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.uav.Get());
                pso_vsp_pre_update_->SetView(&desc_set, "RWVspProbePoolBuffer", vsp_probe_pool_buffer_.uav.Get());
                pso_vsp_pre_update_->SetView(&desc_set, "RWVspProbeFreeStack", vsp_probe_free_stack_buffer_.uav.Get());
                pso_vsp_pre_update_->SetView(&desc_set, "RWVspActiveProbeListCurr", vsp_active_probe_curr_list.uav.Get());
                pso_vsp_pre_update_->SetView(&desc_set, k_shader_bind_name_vsp_atlas_uav.Get(), vsp_probe_atlas_tex_.uav.Get());


                p_command_list->SetPipelineState(pso_vsp_pre_update_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_pre_update_.Get(), &desc_set);

                p_command_list->DispatchIndirect(vsp_indirect_arg_.buffer.Get());// 可視SurfaceListDispatch.


                p_command_list->ResourceUavBarrier(vsp_cell_probe_index_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_pool_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_free_stack_buffer_.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_active_probe_curr_list.buffer.Get());
                p_command_list->ResourceUavBarrier(vsp_probe_atlas_tex_.texture.Get());
            }
            // Vsp Current Active IndirectArg生成.
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspGenerateActiveIndirectArg");

                vsp_active_probe_curr_list.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);

                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_generate_curr_active_indirect_arg_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_generate_curr_active_indirect_arg_->SetView(&desc_set, "ProbeIndexList", vsp_active_probe_curr_list.srv.Get());
                pso_vsp_generate_curr_active_indirect_arg_->SetView(&desc_set, "RWVspIndirectArg", vsp_indirect_arg_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_generate_curr_active_indirect_arg_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_generate_curr_active_indirect_arg_.Get(), &desc_set);
                pso_vsp_generate_curr_active_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                vsp_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_MultiPass");
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_Request");

                    vsp_probe_ray_request_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_probe_ray_request_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                    pso_vsp_probe_ray_request_->SetView(&desc_set, "VspActiveProbeListCurr", vsp_active_probe_curr_list.srv.Get());
                    pso_vsp_probe_ray_request_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_request_uav.Get(), vsp_probe_ray_request_buffer_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_probe_ray_request_.Get());
                    p_command_list->SetDescriptorSet(pso_vsp_probe_ray_request_.Get(), &desc_set);
                    p_command_list->DispatchIndirect(vsp_indirect_arg_.buffer.Get());

                    p_command_list->ResourceUavBarrier(vsp_probe_ray_request_buffer_.buffer.Get());
                    vsp_probe_ray_request_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
                }
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_FinalizeTraceIndirectArg");

                    vsp_probe_trace_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_probe_finalize_linear_indirect_arg_->SetView(&desc_set, "CounterBuffer", vsp_probe_ray_request_buffer_.srv.Get());
                    pso_vsp_probe_finalize_linear_indirect_arg_->SetView(&desc_set, "RWLinearIndirectArg", vsp_probe_trace_indirect_arg_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_probe_finalize_linear_indirect_arg_.Get());
                    p_command_list->SetDescriptorSet(pso_vsp_probe_finalize_linear_indirect_arg_.Get(), &desc_set);
                    pso_vsp_probe_finalize_linear_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                    p_command_list->ResourceUavBarrier(vsp_probe_trace_indirect_arg_.buffer.Get());
                    vsp_probe_trace_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
                }
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_Trace");

                    vsp_probe_ray_result_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);
                    vsp_probe_pool_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, "VspProbePoolBuffer", vsp_probe_pool_buffer_.srv.Get());
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_request_srv.Get(), vsp_probe_ray_request_buffer_.srv.Get());
                    pso_vsp_probe_ray_trace_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_result_uav.Get(), vsp_probe_ray_result_buffer_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_probe_ray_trace_.Get());
                    p_command_list->SetDescriptorSet(pso_vsp_probe_ray_trace_.Get(), &desc_set);
                    p_command_list->DispatchIndirect(vsp_probe_trace_indirect_arg_.buffer.Get());

                    p_command_list->ResourceUavBarrier(vsp_probe_ray_result_buffer_.buffer.Get());
                    vsp_probe_ray_result_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
                }
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_FinalizeResolveIndirectArg");

                    vsp_probe_resolve_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_probe_finalize_linear_indirect_arg_->SetView(&desc_set, "CounterBuffer", vsp_probe_ray_result_buffer_.srv.Get());
                    pso_vsp_probe_finalize_linear_indirect_arg_->SetView(&desc_set, "RWLinearIndirectArg", vsp_probe_resolve_indirect_arg_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_probe_finalize_linear_indirect_arg_.Get());
                    p_command_list->SetDescriptorSet(pso_vsp_probe_finalize_linear_indirect_arg_.Get(), &desc_set);
                    pso_vsp_probe_finalize_linear_indirect_arg_->DispatchHelper(p_command_list, 1, 1, 1);

                    p_command_list->ResourceUavBarrier(vsp_probe_resolve_indirect_arg_.buffer.Get());
                    vsp_probe_resolve_indirect_arg_.ResourceBarrier(p_command_list, rhi::EResourceState::IndirectArgument);
                }
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspUpdate_Resolve");

                    vsp_probe_pool_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::UnorderedAccess);

                    ngl::rhi::DescriptorSetDep desc_set = {};
                    pso_vsp_probe_ray_resolve_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                    pso_vsp_probe_ray_resolve_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
                    pso_vsp_probe_ray_resolve_->SetView(&desc_set, k_shader_bind_name_vsp_probe_ray_result_srv.Get(), vsp_probe_ray_result_buffer_.srv.Get());
                    pso_vsp_probe_ray_resolve_->SetView(&desc_set, "RWVspProbePoolBuffer", vsp_probe_pool_buffer_.uav.Get());
                    pso_vsp_probe_ray_resolve_->SetView(&desc_set, k_shader_bind_name_vsp_atlas_uav.Get(), vsp_probe_atlas_tex_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_probe_ray_resolve_.Get());
                    p_command_list->SetDescriptorSet(pso_vsp_probe_ray_resolve_.Get(), &desc_set);
                    p_command_list->DispatchIndirect(vsp_probe_resolve_indirect_arg_.buffer.Get());

                    p_command_list->ResourceUavBarrier(vsp_probe_pool_buffer_.buffer.Get());
                    p_command_list->ResourceUavBarrier(vsp_probe_atlas_tex_.texture.Get());
                }
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspProbeShUpdate");

                vsp_probe_atlas_tex_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
                vsp_probe_pool_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_sh_update_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_sh_update_->SetView(&desc_set, "VspActiveProbeListCurr", vsp_active_probe_curr_list.srv.Get());
                pso_vsp_sh_update_->SetView(&desc_set, k_shader_bind_name_vsp_atlas_srv.Get(), vsp_probe_atlas_tex_.srv.Get());
                pso_vsp_sh_update_->SetView(&desc_set, "VspProbePoolBuffer", vsp_probe_pool_buffer_.srv.Get());
                pso_vsp_sh_update_->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_uav.Get(), vsp_irradiance_volume_sh_texture_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_sh_update_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_sh_update_.Get(), &desc_set);
                p_command_list->DispatchIndirect(vsp_indirect_arg_.buffer.Get());

                p_command_list->ResourceUavBarrier(vsp_irradiance_volume_sh_texture_.texture.Get());
            }
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspIrradianceVolumePropagate");

                vsp_cell_probe_index_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
                vsp_probe_pool_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);

                ngl::rhi::DescriptorSetDep desc_set = {};
                pso_vsp_irradiance_volume_propagate_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
                pso_vsp_irradiance_volume_propagate_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
                pso_vsp_irradiance_volume_propagate_->SetView(&desc_set, "VspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.srv.Get());
                pso_vsp_irradiance_volume_propagate_->SetView(&desc_set, "VspProbePoolBuffer", vsp_probe_pool_buffer_.srv.Get());
                pso_vsp_irradiance_volume_propagate_->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_uav.Get(), vsp_irradiance_volume_sh_texture_.uav.Get());

                p_command_list->SetPipelineState(pso_vsp_irradiance_volume_propagate_.Get());
                p_command_list->SetDescriptorSet(pso_vsp_irradiance_volume_propagate_.Get(), &desc_set);
                pso_vsp_irradiance_volume_propagate_->DispatchHelper(p_command_list, vsp_total_cell_count_, 1, 1);

                p_command_list->ResourceUavBarrier(vsp_irradiance_volume_sh_texture_.texture.Get());
                vsp_irradiance_volume_sh_texture_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::ShaderRead);
            }
            if(InstantRasterDerivedVoxelScene::dbg_vsp_debug_readback_enable_)
            {
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspReadbackCopy");

                vsp_debug_stats_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
                {
                    ngl::rhi::DescriptorSetDep stats_desc_set = {};
                    pso_vsp_debug_stats_collect_->SetView(
                        &stats_desc_set,
                        "SurfaceProbeCellList",
                        vsp_visible_surface_list_.srv.Get());
                    pso_vsp_debug_stats_collect_->SetView(
                        &stats_desc_set,
                        "VspProbeFreeStack",
                        vsp_probe_free_stack_buffer_.srv.Get());
                    pso_vsp_debug_stats_collect_->SetView(
                        &stats_desc_set,
                        "VspActiveProbeListCurr",
                        vsp_active_probe_curr_list.srv.Get());
                    pso_vsp_debug_stats_collect_->SetView(
                        &stats_desc_set,
                        "RWVspDebugStats",
                        vsp_debug_stats_buffer_.uav.Get());

                    p_command_list->SetPipelineState(pso_vsp_debug_stats_collect_.Get());
                    p_command_list->SetDescriptorSet(
                        pso_vsp_debug_stats_collect_.Get(),
                        &stats_desc_set);
                    pso_vsp_debug_stats_collect_->DispatchHelper(p_command_list, 1, 1, 1);
                }
                p_command_list->ResourceUavBarrier(vsp_debug_stats_buffer_.buffer.Get());
                vsp_debug_stats_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::CopySrc);
                p_command_list->CopyResource(
                    vsp_debug_stats_readback_buffer_.Get(),
                    vsp_debug_stats_buffer_.buffer.Get());
                vsp_debug_stats_buffer_.ResourceBarrier(
                    p_command_list,
                    rhi::EResourceState::UnorderedAccess);
            }
        }
    }
    
    void BitmaskBrickVoxelGi::Dispatch_Debug(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Dispatch_Debug");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        // デバッグ描画準備.
        if(0 <= InstantRasterDerivedVoxelScene::dbg_view_category_)
        {
            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_bbv_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_bbv_debug_visualize_->SetView(
                &desc_set,
                "TexReducedSurfaceBuffer",
                reduced_surface_buffer_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(
                &desc_set,
                "SmpReducedSurfaceBuffer",
                global_res.default_resource_.sampler_linear_clamp.Get());
            pso_bbv_debug_visualize_->SetView(
                &desc_set,
                "SmpVspIrradianceVolume",
                global_res.default_resource_.sampler_linear_clamp.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_vsp_atlas_srv.Get(), vsp_probe_atlas_tex_.srv.Get());
            vsp_cell_probe_index_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            vsp_probe_pool_buffer_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            pso_bbv_debug_visualize_->SetView(&desc_set, "VspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, "VspProbePoolBuffer", vsp_probe_pool_buffer_.srv.Get());
            vsp_irradiance_volume_sh_texture_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_srv.Get(), vsp_irradiance_volume_sh_texture_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_srv.Get(), assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_variance_srv.Get(), assp_probe_variance_tex_[assp_variance_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_asspprobe_packed_sh_srv.Get(), assp_probe_packed_sh_tex_.srv.Get());
            pso_bbv_debug_visualize_->SetView(&desc_set, k_shader_bind_name_assp_probe_ray_meta_srv.Get(), assp_probe_ray_meta_buffer_.srv.Get());
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
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "InstantRdv_Debug");

        
        // Viewport.
        gfx::helper::SetFullscreenViewportAndScissor(p_command_list, lighting_tex->GetWidth(), lighting_tex->GetHeight());

        // Rtv, Dsv セット.
        {
            const auto* p_rtv = lighting_rtv.Get();
            p_command_list->SetRenderTargets(&p_rtv, 1, hw_depth_dsv.Get());
        }

        if (0 <= InstantRasterDerivedVoxelScene::dbg_bbv_probe_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BbvProbeDebug");

            p_command_list->SetPipelineState(pso_bbv_debug_probe_.Get());
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_bbv_debug_probe_->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);
            
            pso_bbv_debug_probe_->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxelOptionData", bbv_optional_data_buffer_.srv.Get());
            pso_bbv_debug_probe_->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            p_command_list->SetDescriptorSet(pso_bbv_debug_probe_.Get(), &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * bbv_grid_updater_.Get().total_count, 1, 0, 0);
        }
        if (0 <= InstantRasterDerivedVoxelScene::dbg_vsp_probe_debug_mode_ ||
            0 <= InstantRasterDerivedVoxelScene::dbg_vsp_irradiance_volume_debug_mode_)
        {
            NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "VspProbeDebug");

            auto* pso_vsp_debug_probe = (0 != InstantRasterDerivedVoxelScene::dbg_vsp_probe_depth_test_)
                ? pso_vsp_debug_probe_.Get()
                : pso_vsp_debug_probe_no_depth_.Get();
            p_command_list->SetPipelineState(pso_vsp_debug_probe);
            ngl::rhi::DescriptorSetDep desc_set = {};

            pso_vsp_debug_probe->SetView(&desc_set, "cb_ngl_sceneview", &scene_cbv->cbv);

            pso_vsp_debug_probe->SetView(&desc_set, "cb_instant_rdv", &cbh_dispatch_->cbv);
            pso_vsp_debug_probe->SetView(&desc_set, "VspCellProbeIndexBuffer", vsp_cell_probe_index_buffer_.srv.Get());
            pso_vsp_debug_probe->SetView(&desc_set, "VspProbePoolBuffer", vsp_probe_pool_buffer_.srv.Get());
            pso_vsp_debug_probe->SetView(&desc_set, "BitmaskBrickVoxel", bbv_buffer_.srv.Get());
            pso_vsp_debug_probe->SetView(&desc_set, k_shader_bind_name_vsp_atlas_srv.Get(), vsp_probe_atlas_tex_.srv.Get());
            vsp_irradiance_volume_sh_texture_.ResourceBarrier(p_command_list, rhi::EResourceState::ShaderRead);
            pso_vsp_debug_probe->SetView(&desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_srv.Get(), vsp_irradiance_volume_sh_texture_.srv.Get());
            p_command_list->SetDescriptorSet(pso_vsp_debug_probe, &desc_set);

            p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
            p_command_list->DrawInstanced(6 * vsp_total_cell_count_, 1, 0, 0);
        }

    }


    // ----------------------------------------------------------------

    
    InstantRasterDerivedVoxelScene::~InstantRasterDerivedVoxelScene()
    {
        Finalize();
    }

    // 初期化
    bool InstantRasterDerivedVoxelScene::Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u vsp_resolution, float vsp_cell_size, u32 vsp_cascade_count)
    {
        bbvgi_instance_ = new BitmaskBrickVoxelGi();
        BitmaskBrickVoxelGi::InitArg init_arg = {};
        {
            init_arg.voxel_resolution = bbv_resolution;
            init_arg.voxel_size       = bbv_cell_size;

            init_arg.probe_resolution = vsp_resolution;
            init_arg.probe_cell_size  = vsp_cell_size;
            init_arg.probe_cascade_count = vsp_cascade_count;
        }
        if(!bbvgi_instance_->Initialize(p_device, init_arg))
        {
            delete bbvgi_instance_;
            bbvgi_instance_ = nullptr;
            return false;
        }

        is_initialized_ = true;
        dbg_bbv_occupancy_injection_fine_cells_default_ = k_occupancy_injection_default_fine_cells;
        dbg_bbv_occupancy_injection_fine_cells_ = dbg_bbv_occupancy_injection_fine_cells_default_;
        dbg_vsp_cascade_count_ = static_cast<int>(std::clamp<u32>(vsp_cascade_count, 1u, k_vsp_max_cascade_count));
        dbg_vsp_resolution_ = vsp_resolution;
        dbg_vsp_probe_debug_cascade_ = std::clamp(dbg_vsp_probe_debug_cascade_, -1, dbg_vsp_cascade_count_ - 1);
        return true;
    }
    // 破棄
    void InstantRasterDerivedVoxelScene::Finalize()
    {
        if(bbvgi_instance_)
        {
            delete bbvgi_instance_;
            bbvgi_instance_ = nullptr;
        }
        is_initialized_ = false;
    }

    void BitmaskBrickVoxelGi::UpdateVspDebugReadback()
    {
        InstantRasterDerivedVoxelScene::dbg_vsp_probe_pool_size_ = static_cast<int>(vsp_probe_pool_size_);
        if(auto* mapped = vsp_debug_stats_readback_buffer_->MapAs<uint32_t>())
        {
            InstantRasterDerivedVoxelScene::dbg_vsp_visible_surface_cell_count_ = static_cast<int>(mapped[0]);
            InstantRasterDerivedVoxelScene::dbg_vsp_free_probe_count_ = static_cast<int>(mapped[1]);
            InstantRasterDerivedVoxelScene::dbg_vsp_active_probe_count_ = static_cast<int>(mapped[2]);
            vsp_debug_stats_readback_buffer_->Unmap();
        }
        else
        {
            InstantRasterDerivedVoxelScene::dbg_vsp_visible_surface_cell_count_ = 0;
            InstantRasterDerivedVoxelScene::dbg_vsp_free_probe_count_ = 0;
            InstantRasterDerivedVoxelScene::dbg_vsp_active_probe_count_ = 0;
        }
        InstantRasterDerivedVoxelScene::dbg_vsp_allocated_probe_count_ =
            std::max(0, InstantRasterDerivedVoxelScene::dbg_vsp_probe_pool_size_ - InstantRasterDerivedVoxelScene::dbg_vsp_free_probe_count_);
    }

    void BitmaskBrickVoxelGi::UpdateAsspDebugReadback()
    {
        if (assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].texture.Get())
        {
            const auto* p_tex = assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].texture.Get();
            InstantRasterDerivedVoxelScene::dbg_assp_probe_count_ =
                static_cast<int>(p_tex->GetWidth() * p_tex->GetHeight());
        }
        else
        {
            InstantRasterDerivedVoxelScene::dbg_assp_probe_count_ = 0;
        }

        if (assp_probe_total_ray_count_readback_buffer_.Get() == nullptr)
        {
            InstantRasterDerivedVoxelScene::dbg_assp_total_ray_count_ = 0;
            return;
        }
        if (auto* mapped = assp_probe_total_ray_count_readback_buffer_->MapAs<uint32_t>())
        {
            InstantRasterDerivedVoxelScene::dbg_assp_total_ray_count_ = static_cast<int>(mapped[0]);
            assp_probe_total_ray_count_readback_buffer_->Unmap();
            return;
        }
        InstantRasterDerivedVoxelScene::dbg_assp_total_ray_count_ = 0;
    }

    bool InstantRasterDerivedVoxelScene::PrepareFrame(
        rhi::DeviceDep* p_device,
        const math::Vec3& important_pos,
        const math::Vec3& important_dir,
        const math::Vec3& main_light_dir,
        const math::Vec2i& render_resolution,
        int gi_sample_mode)
    {
        if(bbvgi_instance_)
        {
            if(InstantRasterDerivedVoxelScene::dbg_vsp_debug_readback_enable_)
            {
                bbvgi_instance_->UpdateVspDebugReadback();
            }
            bbvgi_instance_->UpdateAsspDebugReadback();
            return bbvgi_instance_->PrepareFrame(
                p_device,
                important_pos,
                important_dir,
                main_light_dir,
                render_resolution,
                gi_sample_mode);
        }
        return false;
    }

    void InstantRasterDerivedVoxelScene::UploadFrameConstants(rhi::DeviceDep* p_device)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->UploadFrameConstants(p_device);
        }
    }

    void InstantRasterDerivedVoxelScene::DispatchBegin(
        rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Begin(p_command_list, scene_cbv);
        }
    }
    void InstantRasterDerivedVoxelScene::DispatchViewBbvOccupancyUpdate(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, 
        const InjectionSourceDepthBufferInfo& depth_buffer_info)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_OccupancyUpdate_View(p_command_list, main_view_info, depth_buffer_info);
        }
    }
    void InstantRasterDerivedVoxelScene::DispatchViewBbvRadianceInjection(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv,
        const ngl::render::task::RenderPassViewInfo& main_view_info,
        const InjectionSourceDepthBufferViewInfo& view_info)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_RadianceInjection_View(p_command_list, main_view_info, view_info);
        }
    }
    void InstantRasterDerivedVoxelScene::DispatchUpdate(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        int gi_sample_mode)
    {
        dbg_gi_update_sample_mode_ = gi_sample_mode;
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Bbv_Main(p_command_list, scene_cbv);
            for(const auto& entry : k_instant_rdv_gi_dispatch_entries)
            {
                if(static_cast<int>(entry.mode) == gi_sample_mode)
                {
                    (bbvgi_instance_->*entry.dispatch_func)(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv);
                    break;
                }
            }
        }
    }

    void InstantRasterDerivedVoxelScene::DispatchDebug(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->Dispatch_Debug(p_command_list, scene_cbv, main_view_info, hw_depth_tex, hw_depth_srv, work_tex, work_uav);
        }
    }

    void InstantRasterDerivedVoxelScene::DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
        rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv)
    {
        if(bbvgi_instance_)
        {
            bbvgi_instance_->DebugDraw(p_command_list, scene_cbv, hw_depth_tex, hw_depth_dsv, lighting_tex, lighting_rtv);
        }
    }

    void InstantRasterDerivedVoxelScene::SetDescriptor(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const
    {
        assert(bbvgi_instance_);
        p_pso->SetView(p_desc_set, k_shader_bind_name_vsp_atlas_srv.Get(), bbvgi_instance_->GetVspProbeAtlasTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_vsp_irradiance_volume_sh_srv.Get(), bbvgi_instance_->GetVspIrradianceVolumeSHTexture().Get());
        p_pso->SetView(p_desc_set, "VspCellProbeIndexBuffer", bbvgi_instance_->GetVspCellProbeIndexBuffer().Get());
        p_pso->SetView(p_desc_set, "VspProbePoolBuffer", bbvgi_instance_->GetVspProbePoolBuffer().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_asspprobe_tile_info_srv.Get(), bbvgi_instance_->GetAsspProbeTileInfoTex().Get());
        p_pso->SetView(p_desc_set, k_shader_bind_name_asspprobe_packed_sh_srv.Get(), bbvgi_instance_->GetAsspProbePackedShTex().Get());
        p_pso->SetView(p_desc_set, "cb_instant_rdv", &bbvgi_instance_->GetDispatchCbh()->cbv);
    }

}  // namespace ngl::render::app

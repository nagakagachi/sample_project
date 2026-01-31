#pragma once

#include "gfx/command_helper.h"
#include "gfx/rtg/graph_builder.h"

namespace ngl
{
    namespace gfx
    {
        class SceneRepresentation;
        class RtSceneManager;
    }  // namespace gfx
}  // namespace ngl

namespace ngl
{
    namespace render
    {
        namespace app
        {
            class SsVg;
        }
    }  // namespace render
}  // namespace ngl

namespace ngl::test
{
    struct RenderFeatureLighting
    {
        math::Vec3 directional_light_dir  = -math::Vec3::UnitY();
        float directional_light_intensity = math::k_pi_f;
        float sky_light_intensity         = 1.0f;
    };
    struct RenderFeatureGtaoDemo
    {
        bool enable = true;
    };
    struct RenderFeatureGi
    {
        render::app::SsVg* p_ssvg = {};
        bool enable_gi_lighting   = false;
        float probe_sample_offset_view{0.0f};
        float probe_sample_offset_surface_normal{0.0f};
        float probe_sample_offset_bent_normal{0.0f};

        bool enable_ssvg_injection_pass{true};
        bool enable_ssvg_rejection_pass{true};
    };
    struct RenderFeatureConfig
    {
        RenderFeatureLighting lighting;
        RenderFeatureGtaoDemo gtao_demo;
        RenderFeatureGi gi;
    };

    struct EDebugBufferMode
    {
        enum Mode : int
        {
            None = -1,

            GBuffer0 = 0,
            GBuffer1,
            GBuffer2,
            GBuffer3,
            HardwareDepth,
            DirectionalShadowAtlas,

            GtaoDemo,
            BentNormalTest,
            SsvgDebugTexture,

            _MAX
        };
    };

    // RenderPathの入力.
    struct RenderFrameDesc
    {
        // Viewのカメラ情報.
        ngl::math::Vec3 camera_pos   = {};
        ngl::math::Mat33 camera_pose = ngl::math::Mat33::Identity();
        float camera_fov_y           = ngl::math::Deg2Rad(60.0f);

        ngl::rhi::DeviceDep* p_device = {};

        // 描画解像度.
        ngl::u32 screen_w = 0;
        ngl::u32 screen_h = 0;

        // 外部リソースとしてSwapchain情報.
        ngl::rhi::RhiRef<ngl::rhi::SwapChainDep> ref_swapchain = {};
        ngl::rhi::RefRtvDep ref_swapchain_rtv                  = {};
        ngl::rhi::EResourceState swapchain_state_prev          = {};
        ngl::rhi::EResourceState swapchain_state_next          = {};

        const ngl::gfx::SceneRepresentation* p_scene = {};

        // 描画機能系.
        RenderFeatureConfig feature_config = {};

        // テスト用外部テクスチャ.
        ngl::rhi::RefSrvDep ref_test_tex_srv = {};
        // RaytraceScene.
        gfx::RtSceneManager* p_rt_scene = {};
        // 前フレームでの結果ヒストリ.
        ngl::rtg::RtgResourceHandle h_prev_lit = {};
        // 先行する別のrtgの出力をPropagateして使うテスト.
        ngl::rtg::RtgResourceHandle h_other_graph_out_tex = {};

        // デバッグ用設定.
        bool debug_multithread_render_pass       = true;
        bool debug_multithread_cascade_shadow    = true;
        bool debugview_halfdot_gray              = false;
        bool debugview_enable_feedback_blur_test = false;
        bool debugview_subview_result            = false;
        bool debugview_raytrace_result           = false;
        bool debugview_gbuffer                   = false;
        bool debugview_dshadow                   = false;
        bool debugview_ssvg_sky_visibility       = false;

        int debugview_general_debug_buffer  = -1;  // EDebugBufferMode
        int debugview_general_debug_channel = 0;
        float debugview_general_debug_rate  = 0.5f;
    };
    // RenderPathが生成した出力リソース.
    //	このRenderPathとは異なるRtgや次フレームのRtgでアクセス可能.
    //	SubViewレンダリングの結果をMainViewに受け渡すなどの用途.
    struct RenderFrameOut
    {
        ngl::rtg::RtgResourceHandle h_propagate_lit = {};

        float stat_rtg_construct_sec = {};
        float stat_rtg_compile_sec   = {};
        float stat_rtg_execute_sec   = {};
    };

    // RtgによるRenderPathの構築と実行.
    auto TestFrameRenderingPath(
        const RenderFrameDesc& render_frame_desc,
        RenderFrameOut& out_frame_out,
        ngl::rtg::RenderTaskGraphManager& rtg_manager,
        ngl::rtg::RtgSubmitCommandSet* out_command_set) -> void;

}  // namespace ngl::test

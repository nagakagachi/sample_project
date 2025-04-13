#pragma once


#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"


namespace ngl::gfx
{
	class SceneRepresentation;
	class RtSceneManager;
}

namespace ngl::test
{
	// RenderPathの入力.
    struct RenderFrameDesc
    {
        // Viewのカメラ情報.
        ngl::math::Vec3		camera_pos = {};
        ngl::math::Mat33	camera_pose = ngl::math::Mat33::Identity();
        float				camera_fov_y = ngl::math::Deg2Rad(60.0f);
			
        ngl::rhi::DeviceDep* p_device = {};

        // 描画解像度.
        ngl::u32 screen_w = 0;
        ngl::u32 screen_h = 0;

        // 外部リソースとしてSwapchain情報.
        ngl::rhi::RhiRef<ngl::rhi::SwapChainDep> ref_swapchain = {};
        ngl::rhi::RefRtvDep		ref_swapchain_rtv = {};
        ngl::rhi::EResourceState	swapchain_state_prev = {};
        ngl::rhi::EResourceState	swapchain_state_next = {};
    	
        const ngl::gfx::SceneRepresentation* p_scene = {};
    	math::Vec3	directional_light_dir = -math::Vec3::UnitY();

    	// RaytraceScene.
    	gfx::RtSceneManager* p_rt_scene = {};

        ngl::rhi::RefSrvDep ref_test_tex_srv = {};

    	// 前フレームでの結果ヒストリ.
        ngl::rtg::RtgResourceHandle	h_prev_lit = {};
    	// 先行する別のrtgの出力をPropagateして使うテスト.
    	ngl::rtg::RtgResourceHandle	h_other_graph_out_tex = {};


    	bool debug_pass_render_parallel = true;
    	bool debugview_halfdot_gray = false;
    	bool debugview_enable_feedback_blur_test = false;
    	bool debugview_subview_result = false;
    	bool debugview_raytrace_result = false;
    	bool debugview_gbuffer = false;
    	bool debugview_dshadow = false;
    };
	// RenderPathが生成した出力リソース.
	//	このRenderPathとは異なるRtgや次フレームのRtgでアクセス可能.
	//	SubViewレンダリングの結果をMainViewに受け渡すなどの用途.
    struct RenderFrameOut
    {
        ngl::rtg::RtgResourceHandle h_propagate_lit = {};

    	float	stat_rtg_construct_sec = {};
    	float	stat_rtg_compile_sec = {};
    	float	stat_rtg_execute_sec = {};
    };
	
    // RtgによるRenderPathの構築と実行.
	auto TestFrameRenderingPath(
			const RenderFrameDesc& render_frame_desc,
			RenderFrameOut& out_frame_out,
			ngl::rtg::RenderTaskGraphManager& rtg_manager,
			std::vector<ngl::rtg::RtgSubmitCommandSequenceElem>& out_graphics_cmd,
			std::vector<ngl::rtg::RtgSubmitCommandSequenceElem>& out_compute_cmd
		) -> void;
    
}


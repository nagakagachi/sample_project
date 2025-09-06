#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

namespace ngl::render::task
{
	// 最終パス.
	struct TaskFinalPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_linear_depth_{};
		rtg::RtgResourceHandle h_light_{};
		rtg::RtgResourceHandle h_swapchain_{};

		rtg::RtgResourceHandle h_other_rtg_out_{};// 先行する別rtgがPropagateしたハンドルをそのフレームの後段のrtgで使用するテスト.
		rtg::RtgResourceHandle h_rt_result_{};
		
		rtg::RtgResourceHandle h_gbuffer0_{};// Debug View用
		rtg::RtgResourceHandle h_gbuffer1_{};// Debug View用
		rtg::RtgResourceHandle h_gbuffer2_{};// Debug View用
		rtg::RtgResourceHandle h_gbuffer3_{};// Debug View用
		rtg::RtgResourceHandle h_dshadow_{};// Debug View用
		
		rtg::RtgResourceHandle h_tmp_{}; // 一時リソーステスト. マクロにも登録しない.
		
		rhi::RhiRef<rhi::GraphicsPipelineStateDep> pso_;

		struct SetupDesc
		{
			int w{};
			int h{};
			
			rhi::ConstantBufferPooledHandle scene_cbv{};

			bool debugview_halfdot_gray = false;
			bool debugview_subview_result = false;
			bool debugview_raytrace_result = false;
			
			bool debugview_gbuffer = false;
			bool debugview_dshadow = false;
		};
		SetupDesc desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_swapchain, rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_linear_depth, rtg::RtgResourceHandle h_light,
			rtg::RtgResourceHandle h_other_rtg_out, rtg::RtgResourceHandle h_rt_result,
			rtg::RtgResourceHandle h_gbuffer0, rtg::RtgResourceHandle h_gbuffer1, rtg::RtgResourceHandle h_gbuffer2, rtg::RtgResourceHandle h_gbuffer3,
			rtg::RtgResourceHandle h_dshadow,

			ngl::rhi::RefSrvDep ref_test_tex,

			const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::SHADER_READ);
				h_linear_depth_ = builder.RecordResourceAccess(*this, h_linear_depth, rtg::access_type::SHADER_READ);
				h_light_ = builder.RecordResourceAccess(*this, h_light, rtg::access_type::SHADER_READ);

				h_swapchain_ = builder.RecordResourceAccess(*this, h_swapchain, rtg::access_type::RENDER_TARGET);

				if(!h_rt_result.IsInvalid())
				{
					h_rt_result_ = builder.RecordResourceAccess(*this, h_rt_result, rtg::access_type::SHADER_READ);
				}
				if(!h_other_rtg_out.IsInvalid())
				{
					h_other_rtg_out_ = builder.RecordResourceAccess(*this, h_other_rtg_out, rtg::access_type::SHADER_READ);
				}

				if(!h_gbuffer0.IsInvalid())
					h_gbuffer0_ = builder.RecordResourceAccess(*this, h_gbuffer0, rtg::access_type::SHADER_READ);
				if(!h_gbuffer1.IsInvalid())
					h_gbuffer1_ = builder.RecordResourceAccess(*this, h_gbuffer1, rtg::access_type::SHADER_READ);
				if(!h_gbuffer2.IsInvalid())
					h_gbuffer2_ = builder.RecordResourceAccess(*this, h_gbuffer2, rtg::access_type::SHADER_READ);
				if(!h_gbuffer3.IsInvalid())
					h_gbuffer3_ = builder.RecordResourceAccess(*this, h_gbuffer3, rtg::access_type::SHADER_READ);
				if(!h_dshadow.IsInvalid())
					h_dshadow_ = builder.RecordResourceAccess(*this, h_dshadow, rtg::access_type::SHADER_READ);
				
				// リソースアクセス期間による再利用のテスト用. 作業用の一時リソース.
				rtg::RtgResourceDesc2D temp_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R11G11B10_FLOAT);
				auto temp_res0 = builder.RecordResourceAccess(*this, builder.CreateResource(temp_desc), rtg::access_type::RENDER_TARGET);
				h_tmp_ = temp_res0;
			}
			
			// pso生成のためにRenderTarget(実際はSwapchain)のDescをBuilderから取得. DescはCompile前に取得ができるものとする.
			// (実リソース再利用割当のために実際のリソースのWidthやHeightは取得できないが...).
			const auto render_target_desc = builder.GetResourceHandleDesc(h_swapchain);

			{
				// 初期化. シェーダバイナリの要求とPSO生成.

				auto& ResourceMan = ngl::res::ResourceManager::Instance();

				ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
				{
					loaddesc_vs.entry_point_name = "main_vs";
					loaddesc_vs.stage = ngl::rhi::EShaderStage::Vertex;
					loaddesc_vs.shader_model_version = k_shader_model;
				}
				auto res_shader_vs = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("screen/fullscr_procedural_z1_vs.hlsl"), &loaddesc_vs);

				ngl::gfx::ResShader::LoadDesc loaddesc_ps = {};
				{
					loaddesc_ps.entry_point_name = "main_ps";
					loaddesc_ps.stage = ngl::rhi::EShaderStage::Pixel;
					loaddesc_ps.shader_model_version = k_shader_model;
				}
				auto res_shader_ps = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("final_screen_pass_ps.hlsl"), &loaddesc_ps);


				ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
				desc.vs = &res_shader_vs->data_;
				desc.ps = &res_shader_ps->data_;

				desc.num_render_targets = 1;
				desc.render_target_formats[0] = render_target_desc.desc.format;

				desc.blend_state.target_blend_states[0].blend_enable = false;
				desc.blend_state.target_blend_states[0].write_mask = ~ngl::u8(0);

				pso_.Reset(new rhi::GraphicsPipelineStateDep());
				if (!pso_->Initialize(p_device, desc))
				{
					assert(false);
				}
			}
			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, ref_test_tex](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					if(is_render_skip_debug)
					{
						return;
					}
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "Final");
						
					auto& global_res = gfx::GlobalRenderResource::Instance();
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
					auto res_light = builder.GetAllocatedResource(this, h_light_);
					auto res_swapchain = builder.GetAllocatedResource(this, h_swapchain_);
					auto res_tmp = builder.GetAllocatedResource(this, h_tmp_);
					auto res_other_rtg_out = builder.GetAllocatedResource(this, h_other_rtg_out_);
					auto res_rt_result = builder.GetAllocatedResource(this, h_rt_result_);
						
					auto res_gbuffer0 = builder.GetAllocatedResource(this, h_gbuffer0_);
					auto res_gbuffer1 = builder.GetAllocatedResource(this, h_gbuffer1_);
					auto res_gbuffer2 = builder.GetAllocatedResource(this, h_gbuffer2_);
					auto res_gbuffer3 = builder.GetAllocatedResource(this, h_gbuffer3_);
					auto res_dshadow = builder.GetAllocatedResource(this, h_dshadow_);

					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
					assert(res_linear_depth.tex_.IsValid() && res_linear_depth.srv_.IsValid());
					assert(res_light.tex_.IsValid() && res_light.srv_.IsValid());
					assert(res_swapchain.swapchain_.IsValid() && res_swapchain.rtv_.IsValid());
					assert(res_tmp.tex_.IsValid() && res_tmp.rtv_.IsValid());

					rhi::RefSrvDep ref_other_rtg_out{};
					if(res_other_rtg_out.srv_.IsValid())
					{
						ref_other_rtg_out = res_other_rtg_out.srv_;
					}
					else
					{
						ref_other_rtg_out = global_res.default_resource_.tex_red->ref_view_;
					}
						
					rhi::RefSrvDep ref_rt_result{};
					if(res_rt_result.srv_.IsValid())
					{
						ref_rt_result = res_rt_result.srv_;
					}
					else
					{
						ref_rt_result = global_res.default_resource_.tex_green->ref_view_;
					}

					rhi::RefSrvDep ref_gbuffer0 = (res_gbuffer0.srv_.IsValid())? res_gbuffer0.srv_ : global_res.default_resource_.tex_black->ref_view_;
					rhi::RefSrvDep ref_gbuffer1 = (res_gbuffer1.srv_.IsValid())? res_gbuffer1.srv_ : global_res.default_resource_.tex_black->ref_view_;
					rhi::RefSrvDep ref_gbuffer2 = (res_gbuffer2.srv_.IsValid())? res_gbuffer2.srv_ : global_res.default_resource_.tex_black->ref_view_;
					rhi::RefSrvDep ref_gbuffer3 = (res_gbuffer3.srv_.IsValid())? res_gbuffer3.srv_ : global_res.default_resource_.tex_black->ref_view_;
					rhi::RefSrvDep ref_dshadow = (res_dshadow.srv_.IsValid())? res_dshadow.srv_ : global_res.default_resource_.tex_black->ref_view_;

						
					struct CbFinalScreenPass
					{
						int enable_halfdot_gray;
						int enable_subview_result;
						int enable_raytrace_result;
						int enable_gbuffer;
						int enable_dshadow;
					};
					auto cbh = gfx_commandlist->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(CbFinalScreenPass));
					if(auto* p_mapped = cbh->buffer_.MapAs<CbFinalScreenPass>())
					{
						p_mapped->enable_halfdot_gray = desc_.debugview_halfdot_gray;
						p_mapped->enable_subview_result = desc_.debugview_subview_result;
						p_mapped->enable_raytrace_result = desc_.debugview_raytrace_result;

						p_mapped->enable_gbuffer = desc_.debugview_gbuffer;
						p_mapped->enable_dshadow = desc_.debugview_dshadow;

						cbh->buffer_.Unmap();
					}
						
					gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_swapchain.swapchain_->GetWidth(), res_swapchain.swapchain_->GetHeight());

					// Rtv, Dsv セット.
					{
						const auto* p_rtv = res_swapchain.rtv_.Get();
						gfx_commandlist->SetRenderTargets(&p_rtv, 1, nullptr);
					}

					gfx_commandlist->SetPipelineState(pso_.Get());
					ngl::rhi::DescriptorSetDep desc_set = {};
					pso_->SetView(&desc_set, "cb_final_screen_pass", &cbh->cbv_);
					pso_->SetView(&desc_set, "tex_light", res_light.srv_.Get());
					pso_->SetView(&desc_set, "tex_rt", ref_rt_result.Get());

#if 1
					// 別Viewの結果を貼り付け.
					pso_->SetView(&desc_set, "tex_res_data", ref_other_rtg_out.Get());
#else
					// テクスチャリソースを貼り付け.
					pso_->SetView(&desc_set, "tex_res_data", ref_test_tex.Get());
#endif

					pso_->SetView(&desc_set, "tex_gbuffer0", ref_gbuffer0.Get());
					pso_->SetView(&desc_set, "tex_gbuffer1", ref_gbuffer1.Get());
					pso_->SetView(&desc_set, "tex_gbuffer2", ref_gbuffer2.Get());
					pso_->SetView(&desc_set, "tex_gbuffer3", ref_gbuffer3.Get());
					pso_->SetView(&desc_set, "tex_dshadow", ref_dshadow.Get());
						
					pso_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
					gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

					gfx_commandlist->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
					gfx_commandlist->DrawInstanced(3, 1, 0, 0);
				});
		}
	};
}
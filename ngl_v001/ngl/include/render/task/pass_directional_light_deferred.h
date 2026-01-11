/*
    Directional Light Deferred Lighting Pass.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "render/app/ssvg/ssvg.h"


namespace ngl::render::task
{
	// Lightingパス.
	struct TaskLightPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_gb0_{};
		rtg::RtgResourceHandle h_gb1_{};
		rtg::RtgResourceHandle h_gb2_{};
		rtg::RtgResourceHandle h_gb3_{};
		rtg::RtgResourceHandle h_velocity_{};
		rtg::RtgResourceHandle h_linear_depth_{};
		rtg::RtgResourceHandle h_prev_light_{};
		rtg::RtgResourceHandle h_light_{};
		
		rtg::RtgResourceHandle h_shadowmap_{};
		
		
		rhi::RhiRef<rhi::GraphicsPipelineStateDep> pso_;

		struct SetupDesc
		{
			int w{};
			int h{};
			rhi::ConstantBufferPooledHandle scene_cbv{};
			rhi::ConstantBufferPooledHandle ref_shadow_cbv{};

			fwk::GfxScene* scene{};
			fwk::GfxSceneEntityId skybox_proxy_id{};
			
            render::app::SsVg* p_ssvg = {};
            bool is_enable_gi_lighting = false;

			bool enable_feedback_blur_test{};
            bool dbg_view_ssvg_sky_visibility = false;
		} desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_light,
			rtg::RtgResourceHandle h_gb0, rtg::RtgResourceHandle h_gb1, rtg::RtgResourceHandle h_gb2, rtg::RtgResourceHandle h_gb3, rtg::RtgResourceHandle h_velocity,
			rtg::RtgResourceHandle h_linear_depth, rtg::RtgResourceHandle h_prev_light,
			rtg::RtgResourceHandle h_shadowmap,
			rtg::RtgResourceHandle h_async_compute_result,
			const SetupDesc& desc)
		{
			// Rtgリソースセットアップ.
			{
				desc_ = desc;
				
				// リソースアクセス定義.
				h_gb0_ = builder.RecordResourceAccess(*this, h_gb0, rtg::access_type::SHADER_READ);
				h_gb1_ = builder.RecordResourceAccess(*this, h_gb1, rtg::access_type::SHADER_READ);
				h_gb2_ = builder.RecordResourceAccess(*this, h_gb2, rtg::access_type::SHADER_READ);
				h_gb3_ = builder.RecordResourceAccess(*this, h_gb3, rtg::access_type::SHADER_READ);
				h_velocity_ = builder.RecordResourceAccess(*this, h_velocity, rtg::access_type::SHADER_READ);
				h_linear_depth_ = builder.RecordResourceAccess(*this, h_linear_depth, rtg::access_type::SHADER_READ);
				h_shadowmap_ = builder.RecordResourceAccess(*this, h_shadowmap, rtg::access_type::SHADER_READ);

				h_prev_light_ = {};
				if(!h_prev_light.IsInvalid())
				{
					h_prev_light_ = builder.RecordResourceAccess(*this, h_prev_light, rtg::access_type::SHADER_READ);
				}
				if(!h_async_compute_result.IsInvalid())
				{
					// Asyncの結果を読み取りだけレコードしてFenceさせる.
					builder.RecordResourceAccess(*this, h_async_compute_result, rtg::access_type::SHADER_READ);
				}

				if (h_light.IsInvalid())
				{
					rtg::RtgResourceDesc2D light_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
					h_light = builder.CreateResource(light_desc);
				}
				h_light_ = builder.RecordResourceAccess(*this, h_light, rtg::access_type::RENDER_TARGET);// このTaskで新規生成したRenderTargetを出力先とする.
			}
			
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
				auto res_shader_ps = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("df_light_pass_ps.hlsl"), &loaddesc_ps);

				ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
				{
					desc.vs = &res_shader_vs->data_;
					desc.ps = &res_shader_ps->data_;
					{
						desc.num_render_targets = 1;
						desc.render_target_formats[0] = builder.GetResourceHandleDesc(h_light_).desc.format;//light_desc.desc.format;
					}
				}
				pso_.Reset(new rhi::GraphicsPipelineStateDep());
				if (!pso_->Initialize(p_device, desc))
				{
					assert(false);
				}
			}
			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					if(is_render_skip_debug)
					{
						return;
					}
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "Lighting");
						
					auto& global_res = gfx::GlobalRenderResource::Instance();
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_gb0 = builder.GetAllocatedResource(this, h_gb0_);
					auto res_gb1 = builder.GetAllocatedResource(this, h_gb1_);
					auto res_gb2 = builder.GetAllocatedResource(this, h_gb2_);
					auto res_gb3 = builder.GetAllocatedResource(this, h_gb3_);
					auto res_velocity = builder.GetAllocatedResource(this, h_velocity_);

					auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
					auto res_prev_light = builder.GetAllocatedResource(this, h_prev_light_);// 前回フレームリソースのテスト.
					auto res_light = builder.GetAllocatedResource(this, h_light_);
					auto res_shadowmap = builder.GetAllocatedResource(this, h_shadowmap_);
						
					assert(res_gb0.tex_.IsValid() && res_gb0.srv_.IsValid());
					assert(res_gb1.tex_.IsValid() && res_gb1.srv_.IsValid());
					assert(res_gb2.tex_.IsValid() && res_gb2.srv_.IsValid());
					assert(res_gb3.tex_.IsValid() && res_gb3.srv_.IsValid());
					assert(res_velocity.tex_.IsValid() && res_velocity.srv_.IsValid());
					assert(res_linear_depth.tex_.IsValid() && res_linear_depth.srv_.IsValid());
					assert(res_light.tex_.IsValid() && res_light.rtv_.IsValid());
					assert(res_shadowmap.tex_.IsValid() && res_shadowmap.srv_.IsValid());

					// 前回フレームのライトリソースが無効な場合は、グローバルリソースのデフォルトを使用.
					rhi::RefSrvDep ref_prev_lit = (res_prev_light.srv_.IsValid())? res_prev_light.srv_ : global_res.default_resource_.tex_black->ref_view_;

					// SkyboxProxyから情報取り出し.
					auto* skybox_proxy = desc_.scene->buffer_skybox_.proxy_buffer_[desc_.skybox_proxy_id.GetIndex()];
					

                    static bool debug_first_frame_flag = true;
                    const bool is_first_frame = debug_first_frame_flag;
                    debug_first_frame_flag = false;


					// LightingPass定数バッファ.
					struct CbLightingPass
					{
						int enable_feedback_blur_test{};
						int is_first_frame{};
                        int is_enable_gi{};
                        int dbg_view_ssvg_sky_visibility{};
					};
					auto lighting_cbh = gfx_commandlist->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(CbLightingPass));
					if(auto* p_mapped = lighting_cbh->buffer_.MapAs<CbLightingPass>())
					{
						p_mapped->enable_feedback_blur_test = desc_.enable_feedback_blur_test;
						p_mapped->is_first_frame = is_first_frame ? 1 : 0;

                        p_mapped->is_enable_gi = (desc_.p_ssvg != nullptr && desc_.is_enable_gi_lighting) ? 1 : 0;
                        p_mapped->dbg_view_ssvg_sky_visibility = desc_.dbg_view_ssvg_sky_visibility ? 1 : 0;

						lighting_cbh->buffer_.Unmap();
					}
					
					// Viewport.
					gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_light.tex_->GetWidth(), res_light.tex_->GetHeight());

					// Rtv, Dsv セット.
					{
						const auto* p_rtv = res_light.rtv_.Get();
						gfx_commandlist->SetRenderTargets(&p_rtv, 1, nullptr);
					}

					gfx_commandlist->SetPipelineState(pso_.Get());
					ngl::rhi::DescriptorSetDep desc_set = {};

					pso_->SetView(&desc_set, "cb_ngl_sceneview", &desc_.scene_cbv->cbv_);
					pso_->SetView(&desc_set, "cb_ngl_shadowview", &desc_.ref_shadow_cbv->cbv_);
					pso_->SetView(&desc_set, "cb_ngl_lighting_pass", &lighting_cbh->cbv_);
						
					pso_->SetView(&desc_set, "tex_lineardepth", res_linear_depth.srv_.Get());
					pso_->SetView(&desc_set, "tex_gbuffer0", res_gb0.srv_.Get());
					pso_->SetView(&desc_set, "tex_gbuffer1", res_gb1.srv_.Get());
					pso_->SetView(&desc_set, "tex_gbuffer2", res_gb2.srv_.Get());
					pso_->SetView(&desc_set, "tex_gbuffer3", res_gb3.srv_.Get());
						
					pso_->SetView(&desc_set, "tex_prev_light", ref_prev_lit.Get());

					pso_->SetView(&desc_set, "tex_shadowmap", res_shadowmap.srv_.Get());

					pso_->SetView(&desc_set, "tex_ibl_diffuse", skybox_proxy->ibl_diffuse_cubemap_plane_array_srv_.Get());
					pso_->SetView(&desc_set, "tex_ibl_specular", skybox_proxy->ibl_ggx_specular_cubemap_plane_array_srv_.Get());
					pso_->SetView(&desc_set, "tex_ibl_dfg", skybox_proxy->ibl_ggx_dfg_lut_srv_.Get());

					pso_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_clamp.Get());
					pso_->SetView(&desc_set, "samp_shadow", gfx::GlobalRenderResource::Instance().default_resource_.sampler_shadow_linear.Get());


                    if(desc_.p_ssvg)
                    {
                        desc_.p_ssvg->SetDescriptor(pso_.Get(), &desc_set);
                    }

						
					gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

					gfx_commandlist->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
					gfx_commandlist->DrawInstanced(3, 1, 0, 0);
				});
		}
	};
}
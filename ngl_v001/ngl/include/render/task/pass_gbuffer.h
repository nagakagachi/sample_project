/*
    GBufferパス.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "gfx/material/material_shader_manager.h"
#include "gfx/rendering/mesh_renderer.h"

namespace ngl::render::task
{
	// GBufferパス.
	struct TaskGBufferPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_gb0_{};
		rtg::RtgResourceHandle h_gb1_{};
		rtg::RtgResourceHandle h_gb2_{};
		rtg::RtgResourceHandle h_gb3_{};
		rtg::RtgResourceHandle h_velocity_{};
		
		struct SetupDesc
		{
			int w{};
			int h{};
			
			rhi::ConstantBufferPooledHandle scene_cbv{};
			
			fwk::GfxScene* gfx_scene{};
			const std::vector<fwk::GfxSceneEntityId>* p_mesh_proxy_id_array_{};
		} desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_async_write_tex,
			const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// MaterialPassに合わせてFormat設定.
				constexpr auto k_gbuffer0_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer0_format;
				constexpr auto k_gbuffer1_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer1_format;
				constexpr auto k_gbuffer2_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer2_format;
				constexpr auto k_gbuffer3_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer3_format;
				constexpr auto k_velocity_format = gfx::MaterialPassPsoCreator_gbuffer::k_velocity_format;
				
				// GBuffer0 BaseColor.xyz, Occlusion.w
				rtg::RtgResourceDesc2D gbuffer0_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer0_format);
				// GBuffer1 WorldNormal.xyz, 1bitOption.w
				rtg::RtgResourceDesc2D gbuffer1_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer1_format);
				// GBuffer2 Roughness, Metallic, Optional, MaterialId
				rtg::RtgResourceDesc2D gbuffer2_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer2_format);
				// GBuffer3 Emissive.xyz, Unused.w
				rtg::RtgResourceDesc2D gbuffer3_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer3_format);
				// Velocity xy
				rtg::RtgResourceDesc2D velocity_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_velocity_format);

				// DepthのFormat取得.
				const auto depth_desc = builder.GetResourceHandleDesc(h_depth);
			
				// リソースアクセス定義.
				if(!h_async_write_tex.IsInvalid())
				{
					// 試しにAsyncComputeで書き込まれたリソースを読み取り.
					builder.RecordResourceAccess(*this, h_async_write_tex, rtg::AccessType::SHADER_READ);
				}
			
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::AccessType::DEPTH_TARGET);
				h_gb0_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer0_desc), rtg::AccessType::RENDER_TARGET);
				h_gb1_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer1_desc), rtg::AccessType::RENDER_TARGET);
				h_gb2_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer2_desc), rtg::AccessType::RENDER_TARGET);
				h_gb3_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer3_desc), rtg::AccessType::RENDER_TARGET);
				h_velocity_ = builder.RecordResourceAccess(*this, builder.CreateResource(velocity_desc), rtg::AccessType::RENDER_TARGET);
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "GBuffer");
			
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_gb0 = builder.GetAllocatedResource(this, h_gb0_);
					auto res_gb1 = builder.GetAllocatedResource(this, h_gb1_);
					auto res_gb2 = builder.GetAllocatedResource(this, h_gb2_);
					auto res_gb3 = builder.GetAllocatedResource(this, h_gb3_);
					auto res_velocity = builder.GetAllocatedResource(this, h_velocity_);

					assert(res_depth.tex_.IsValid() && res_depth.dsv_.IsValid());
					assert(res_gb0.tex_.IsValid() && res_gb0.rtv_.IsValid());
					assert(res_gb1.tex_.IsValid() && res_gb1.rtv_.IsValid());
					assert(res_gb2.tex_.IsValid() && res_gb2.rtv_.IsValid());
					assert(res_gb3.tex_.IsValid() && res_gb3.rtv_.IsValid());
					assert(res_velocity.tex_.IsValid() && res_velocity.rtv_.IsValid());

					const rhi::RenderTargetViewDep* p_targets[] =
					{
						res_gb0.rtv_.Get(),
						res_gb1.rtv_.Get(),
						res_gb2.rtv_.Get(),
						res_gb3.rtv_.Get(),
						res_velocity.rtv_.Get(),
					};
			
					// Set RenderTarget.
					gfx_commandlist->SetRenderTargets(p_targets, (int)std::size(p_targets), res_depth.dsv_.Get());
					// Set Viewport and Scissor.
					ngl::gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_depth.tex_->GetWidth(), res_depth.tex_->GetHeight());

					// Mesh Rendering.
					gfx::RenderMeshResource render_mesh_res = {};
					{
						render_mesh_res.cbv_sceneview = {"cb_ngl_sceneview", &desc_.scene_cbv->cbv_};
					}
                    ngl::gfx::RenderMeshWithMaterial(*gfx_commandlist, gfx::MaterialPassPsoCreator_gbuffer::k_name, desc_.gfx_scene, *desc_.p_mesh_proxy_id_array_, render_mesh_res);
				}
			);
		}
	};
}
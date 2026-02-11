/*
    LinearDepthパス.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

namespace ngl::render::task
{
	// LinearDepthパス.
	struct TaskLinearDepthPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_linear_depth_{};
;
		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_;

		struct SetupDesc
		{
			int w{};
			int h{};
			
			rhi::ConstantBufferPooledHandle scene_cbv{};
		} desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_tex_compute, const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				rtg::RtgResourceDesc2D linear_depth_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32_FLOAT);

				// Async Computeの出力リソースを読み取るテスト.
				if(!h_tex_compute.IsInvalid())
				{
					builder.RecordResourceAccess(*this, h_tex_compute, rtg::AccessType::SHADER_READ);
				}
				
				// リソースアクセス定義.
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::AccessType::SHADER_READ);
				h_linear_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(linear_depth_desc), rtg::AccessType::UAV);
			}

			{
				auto& ResourceMan = ngl::res::ResourceManager::Instance();

				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.entry_point_name = "main_cs";
				loaddesc.stage = ngl::rhi::EShaderStage::Compute;
				loaddesc.shader_model_version = k_shader_model;
				auto res_shader = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("screen/generate_lineardepth_cs.hlsl"), &loaddesc);
				
				ngl::rhi::ComputePipelineStateDep::Desc pso_desc = {};
				pso_desc.cs = &res_shader->data_;
				pso_.Reset(new rhi::ComputePipelineStateDep());
				if (!pso_->Initialize(p_device, pso_desc))
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "LinearDepth");

					auto& global_res = gfx::GlobalRenderResource::Instance();
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);

					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
					assert(res_linear_depth.tex_.IsValid() && res_linear_depth.uav_.IsValid());

					ngl::rhi::DescriptorSetDep desc_set = {};
					pso_->SetView(&desc_set, "TexHardwareDepth", res_depth.srv_.Get());
					// Samplerを設定するテスト. シェーダコード側ではほぼ意味はない.
					pso_->SetView(&desc_set, "SmpHardwareDepth", global_res.default_resource_.sampler_shadow_point.Get());
					pso_->SetView(&desc_set, "RWTexLinearDepth", res_linear_depth.uav_.Get());
					pso_->SetView(&desc_set, "cb_ngl_sceneview", &desc_.scene_cbv->cbv_);
						
					gfx_commandlist->SetPipelineState(pso_.Get());
					gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

					pso_->DispatchHelper(gfx_commandlist, res_linear_depth.tex_->GetWidth(), res_linear_depth.tex_->GetHeight(), 1);
				}
			);
		}
	};
}
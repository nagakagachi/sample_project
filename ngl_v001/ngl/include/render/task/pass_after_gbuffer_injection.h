#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "render/app/ssvg/ssvg.h"

namespace ngl::render::task
{
	// LinearDepthパス.
	struct TaskAfterGBufferInjection : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_work_{};

		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_;

		struct SetupDesc
		{
			int w{};
			int h{};
			
			rhi::ConstantBufferPooledHandle scene_cbv{};
		};
		SetupDesc desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_depth, const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				rtg::RtgResourceDesc2D work_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32G32B32A32_FLOAT);

				// リソースアクセス定義.
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::SHADER_READ);

                h_work_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_desc), rtg::access_type::UAV);
			}

			{
				auto& ResourceMan = ngl::res::ResourceManager::Instance();

				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.entry_point_name = "main_cs";
				loaddesc.stage = ngl::rhi::EShaderStage::Compute;
				loaddesc.shader_model_version = k_shader_model;
				auto res_shader = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("ssvg/ss_voxelize_cs.hlsl"), &loaddesc);
				
				ngl::rhi::ComputePipelineStateDep::Desc pso_desc = {};
				pso_desc.cs = &res_shader->data_;
				pso_ = new rhi::ComputePipelineStateDep();
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "SsVoxelize");

					auto& global_res = gfx::GlobalRenderResource::Instance();


                    struct DispatchParam
                    {
                        ngl::math::Vec2i TexHardwareDepthSize;
                    };
                    auto cbh = gfx_commandlist->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(DispatchParam));
                    {
                        auto* p = cbh->buffer_.MapAs<DispatchParam>();

                        p->TexHardwareDepthSize = ngl::math::Vec2i(static_cast<int>(desc_.w), static_cast<int>(desc_.h));

                        cbh->buffer_.Unmap();
                    }

					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    auto res_work = builder.GetAllocatedResource(this, h_work_);
                    
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                    assert(res_work.tex_.IsValid() && res_work.uav_.IsValid());

					ngl::rhi::DescriptorSetDep desc_set = {};
					pso_->SetView(&desc_set, "TexHardwareDepth", res_depth.srv_.Get());
					pso_->SetView(&desc_set, "ngl_cb_sceneview", &desc_.scene_cbv->cbv_);

                    pso_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);

                    pso_->SetView(&desc_set, "RWTexWork", res_work.uav_.Get());
                    
						
					gfx_commandlist->SetPipelineState(pso_.Get());
					gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

					pso_->DispatchHelper(gfx_commandlist, res_work.tex_->GetWidth(), res_work.tex_->GetHeight(), 1);
				}
			);
		}
	};
}
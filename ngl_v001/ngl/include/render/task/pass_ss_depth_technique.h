/*
    スクリーンスペースのDepthBuffer利用テクニックパス.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

namespace ngl::render::task
{
	// .
	class TaskScreenSpaceDepthTechniquePass : public rtg::IGraphicsTaskNode
	{
	public:
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_linear_depth_{};

		rtg::RtgResourceHandle h_gtao_bent_normal_{};// bent normal.xyz, visibility.w
		rtg::RtgResourceHandle h_bent_normal_{};

		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_gtao_demo_;
		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_bent_normal_;

		struct SetupDesc
		{
			int w{};
			int h{};
			rhi::ConstantBufferPooledHandle scene_cbv{};

            bool enable_gtao_demo = true;
		} desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info
		    ,const SetupDesc& desc
		    ,rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_linear_depth)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソースアクセス定義.
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::AccessType::SHADER_READ);
				h_linear_depth_ = builder.RecordResourceAccess(*this, h_linear_depth, rtg::AccessType::SHADER_READ);
                
				if(desc_.enable_gtao_demo)
                {
					rtg::RtgResourceDesc2D tex_gtao_bent_normal_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc_.w, desc_.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
					h_gtao_bent_normal_ = builder.RecordResourceAccess(*this, builder.CreateResource(tex_gtao_bent_normal_desc), rtg::AccessType::UAV);
                }

				rtg::RtgResourceDesc2D bent_normal_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc_.w, desc_.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
				h_bent_normal_ = builder.RecordResourceAccess(*this, builder.CreateResource(bent_normal_desc), rtg::AccessType::UAV);
			}


            // GTAO デモ.
            {
                auto& ResourceMan = ngl::res::ResourceManager::Instance();

                ngl::gfx::ResShader::LoadDesc loaddesc = {};
                loaddesc.entry_point_name = "main_cs";
                loaddesc.stage = ngl::rhi::EShaderStage::Compute;
                loaddesc.shader_model_version = k_shader_model;
                auto res_shader = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("gtao/generate_gtao_demo_cs.hlsl"), &loaddesc);
                
                ngl::rhi::ComputePipelineStateDep::Desc pso_desc = {};
                pso_desc.cs = &res_shader->data_;
				pso_gtao_demo_.Reset(new rhi::ComputePipelineStateDep());
				if (!pso_gtao_demo_->Initialize(p_device, pso_desc))
                {
                    assert(false);
                }
            }
            // BentNormal.
			{
				auto& ResourceMan = ngl::res::ResourceManager::Instance();

				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.entry_point_name = "main_cs";
				loaddesc.stage = ngl::rhi::EShaderStage::Compute;
				loaddesc.shader_model_version = k_shader_model;
				auto res_shader = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("bent_normal/generate_bent_normal_cs.hlsl"), &loaddesc);
				
				ngl::rhi::ComputePipelineStateDep::Desc pso_desc = {};
				pso_desc.cs = &res_shader->data_;
				pso_bent_normal_.Reset(new rhi::ComputePipelineStateDep());
				if (!pso_bent_normal_->Initialize(p_device, pso_desc))
				{
					assert(false);
				}
			}

			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "ScreenSpaceDepthTechniquePass");

					auto& global_res = gfx::GlobalRenderResource::Instance();
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
					auto res_gtao_bent_normal = builder.GetAllocatedResource(this, h_gtao_bent_normal_);
					auto res_bent_normal = builder.GetAllocatedResource(this, h_bent_normal_);

                    if(res_gtao_bent_normal.srv_.IsValid())
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "Gtao_demo");
                        ngl::rhi::DescriptorSetDep desc_set = {};
						pso_gtao_demo_->SetView(&desc_set, "TexLinearDepth", res_linear_depth.srv_.Get());
						pso_gtao_demo_->SetView(&desc_set, "RWTexGtaoBentNormal", res_gtao_bent_normal.uav_.Get());
						pso_gtao_demo_->SetView(&desc_set, "cb_ngl_sceneview", &desc_.scene_cbv->cbv);
                            
						gfx_commandlist->SetPipelineState(pso_gtao_demo_.Get());
						gfx_commandlist->SetDescriptorSet(pso_gtao_demo_.Get(), &desc_set);

						pso_gtao_demo_->DispatchHelper(gfx_commandlist, res_gtao_bent_normal.tex_->GetWidth(), res_gtao_bent_normal.tex_->GetHeight(), 1);
                    }
                    if(res_bent_normal.srv_.IsValid())
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "CustomBentNormal");
                        ngl::rhi::DescriptorSetDep desc_set = {};
						pso_bent_normal_->SetView(&desc_set, "TexLinearDepth", res_linear_depth.srv_.Get());
						pso_bent_normal_->SetView(&desc_set, "RWTexBentNormal", res_bent_normal.uav_.Get());
						pso_bent_normal_->SetView(&desc_set, "cb_ngl_sceneview", &desc_.scene_cbv->cbv);
                            
						gfx_commandlist->SetPipelineState(pso_bent_normal_.Get());
						gfx_commandlist->SetDescriptorSet(pso_bent_normal_.Get(), &desc_set);

						pso_bent_normal_->DispatchHelper(gfx_commandlist, res_bent_normal.tex_->GetWidth(), res_bent_normal.tex_->GetHeight(), 1);
                    }
				}
			);
		}
	};
}
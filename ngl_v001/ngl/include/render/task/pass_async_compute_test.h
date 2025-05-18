#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

namespace ngl::render::task
{
	// AsyncComputeテスト用タスク (IComputeTaskNode派生).
	struct TaskCopmuteTest : public  rtg::IComputeTaskNode
	{
		rtg::RtgResourceHandle h_work_tex_{};

		ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ = {};

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
			rtg::RtgResourceHandle h_input_test, const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				rtg::RtgResourceDesc2D work_tex_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);

				// リソースアクセス定義.
				h_work_tex_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_tex_desc), rtg::access_type::UAV);

				// 入力リソーステスト.
				if(!h_input_test.IsInvalid())
					builder.RecordResourceAccess(*this, h_input_test, rtg::access_type::SHADER_READ);
			}

			{
				pso_ = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
				{
					ngl::rhi::ComputePipelineStateDep::Desc cpso_desc = {};
					{
						ngl::gfx::ResShader::LoadDesc cs_load_desc = {};
						cs_load_desc.stage = ngl::rhi::EShaderStage::Compute;
						cs_load_desc.shader_model_version = k_shader_model;
						cs_load_desc.entry_point_name = "main_cs";
						auto cs_load_handle = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
							p_device, NGL_RENDER_SHADER_PATH("test/async_task_test_cs.hlsl"), &cs_load_desc
						);
						cpso_desc.cs = &cs_load_handle->data_;
					}
					pso_->Initialize(p_device, cpso_desc);
				}
			}
			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskComputeCommandListAllocator command_list_allocator)
				{
					if(is_render_skip_debug)
					{
						return;
					}
					command_list_allocator.Alloc(1);
					auto commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(commandlist, "ComputeTest");
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_work_tex = builder.GetAllocatedResource(this, h_work_tex_);

					assert(res_work_tex.tex_.IsValid() && res_work_tex.uav_.IsValid());

					commandlist->SetPipelineState(pso_.Get());
						
					ngl::rhi::DescriptorSetDep desc_set = {};
					pso_->SetView(&desc_set, "rwtex_out", res_work_tex.uav_.Get());
					commandlist->SetDescriptorSet(pso_.Get(), &desc_set);
						
					pso_->DispatchHelper(commandlist, res_work_tex.tex_->GetWidth(), res_work_tex.tex_->GetHeight(), 1);
				});
		}
	};
}
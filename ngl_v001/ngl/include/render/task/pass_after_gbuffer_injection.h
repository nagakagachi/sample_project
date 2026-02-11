/*
    GBuffer後の追加処理パス.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"


namespace ngl::render::task
{
	struct TaskAfterGBufferInjection : public rtg::IGraphicsTaskNode
	{
		//rtg::RtgResourceHandle h_work_{};

		struct SetupDesc
		{
			int w{};
			int h{};
			
			rhi::ConstantBufferPooledHandle scene_cbv{};
		} desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_depth, const SetupDesc& desc)
		{
			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				//rtg::RtgResourceDesc2D work_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32G32B32A32_FLOAT);
                //h_work_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_desc), rtg::AccessType::UAV);
			}

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "TaskAfterGBufferInjection");

					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
                    //auto res_work = builder.GetAllocatedResource(this, h_work_);
                    //assert(res_work.tex_.IsValid() && res_work.uav_.IsValid());
				}
			);
		}
	};
}
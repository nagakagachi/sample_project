/*
    AfterLightingパス.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "render/app/ssvg/ssvg.h"

namespace ngl::render::task
{
	// AfterLightingパス.
	struct TaskAfterLightPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_depth_{};
		rtg::RtgResourceHandle h_light_{};
		
		struct SetupDesc
		{
			int w{};
			int h{};
			rhi::ConstantBufferPooledHandle scene_cbv{};

            render::app::SsVg* p_ssvg = {};
		} desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			rtg::RtgResourceHandle h_light,
			rtg::RtgResourceHandle h_depth,
			const SetupDesc& desc)
		{
            if(desc.p_ssvg == nullptr)
            {
                return;
            }

			// Rtgリソースセットアップ.
			{
				desc_ = desc;
				
				// リソースアクセス定義.
				h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::DEPTH_TARGET);
				h_light_ = builder.RecordResourceAccess(*this, h_light, rtg::access_type::RENDER_TARGET);
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "AfterLighting");
						
					auto& global_res = gfx::GlobalRenderResource::Instance();

					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_light = builder.GetAllocatedResource(this, h_light_);

					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
					assert(res_light.tex_.IsValid() && res_light.srv_.IsValid());

                    desc_.p_ssvg->DebugDraw(gfx_commandlist,
                        desc_.scene_cbv,
                        res_depth.tex_, res_depth.dsv_,
                        res_light.tex_, res_light.rtv_);
				});
		}
	};
}
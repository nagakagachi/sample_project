#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "gfx/material/material_shader_manager.h"
#include "gfx/rendering/mesh_renderer.h"

#include "framework/gfx_scene.h"

namespace ngl::render::task
{
    // PreZパス.
    struct TaskDepthPass : public rtg::IGraphicsTaskNode
    {
        rtg::RtgResourceHandle h_depth_{};

        struct SetupDesc
        {
            int w{};
            int h{};
				
            rhi::ConstantBufferPooledHandle scene_cbv{};

            fwk::GfxScene* gfx_scene{};
            const std::vector<fwk::GfxSceneEntityId>* p_mesh_proxy_id_array_{};
        };
        SetupDesc desc_{};
        bool is_render_skip_debug{};
			
        // リソースとアクセスを定義するプリプロセス.
        void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info, const SetupDesc& desc)
        {
            desc_ = desc;
            // Rtgリソースセットアップ.
            {
                rtg::RtgResourceDesc2D depth_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, gfx::MaterialPassPsoCreator_depth::k_depth_format);
                h_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);
            }
				
            // Render処理のLambdaをRtgに登録.
            builder.RegisterTaskNodeRenderFunction(this,
                [this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
                {
                    if(is_render_skip_debug)
                    {
                        return;
                    }
                    command_list_allocator.Alloc(1);
                    auto* commandlist = command_list_allocator.GetOrCreate(0);
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(commandlist, "DepthPass");
						
                    auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    assert(res_depth.tex_.IsValid() && res_depth.dsv_.IsValid());

                    commandlist->ClearDepthTarget(res_depth.dsv_.Get(), 0.0f, 0, true, true);// とりあえずクリアだけ.ReverseZなので0クリア.
                    commandlist->SetRenderTargets(nullptr, 0, res_depth.dsv_.Get());
                    ngl::gfx::helper::SetFullscreenViewportAndScissor(commandlist, res_depth.tex_->GetWidth(), res_depth.tex_->GetHeight());
                    gfx::RenderMeshResource render_mesh_res = {};
                    {
                        render_mesh_res.cbv_sceneview = {"cb_ngl_sceneview", &desc_.scene_cbv->cbv_};
                    }
                    
                    ngl::gfx::RenderMeshWithMaterial(*commandlist, gfx::MaterialPassPsoCreator_depth::k_name, desc_.gfx_scene, *desc_.p_mesh_proxy_id_array_, render_mesh_res);
                });
        }
    };

}
/*
    mesh_renderer.cpp
*/

#include "gfx/rendering/mesh_renderer.h"

#include "gfx/common_struct.h"
#include "gfx/material/material_shader_manager.h"
#include "gfx/rendering/global_render_resource.h"
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"

namespace ngl
{
    namespace gfx
    {
        void RenderMeshWithMaterial(rhi::GraphicsCommandListDep& command_list,
                                    const char* pass_name, fwk::GfxScene* gfx_scene, const std::vector<fwk::GfxSceneEntityId>& mesh_proxy_id_array, const RenderMeshResource& render_mesh_resouce)
        {
            auto default_white_tex_srv  = GlobalRenderResource::Instance().default_resource_.tex_white->ref_view_;
            auto default_black_tex_srv  = GlobalRenderResource::Instance().default_resource_.tex_black->ref_view_;
            auto default_normal_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_default_normal->ref_view_;

            auto* mesh_proxy_buffer = gfx_scene->GetEntityProxyBuffer<fwk::GfxSceneEntityMesh>();
            for (int mesh_comp_i = 0; mesh_comp_i < mesh_proxy_id_array.size(); ++mesh_comp_i)
            {
                const auto proxy_id = mesh_proxy_id_array[mesh_comp_i];
                assert(fwk::GfxSceneEntityId::IsValid(proxy_id));

                auto* mesh_proxy = mesh_proxy_buffer->proxy_buffer_[proxy_id.GetIndex()];
                auto* model      = mesh_proxy->model_;

                auto mesh_instance_cbh = command_list.GetDevice()->GetConstantBufferPool()->Alloc(sizeof(InstanceInfo));
                if (auto* map_ptr = mesh_instance_cbh->buffer_.MapAs<InstanceInfo>())
                {
                    map_ptr->mtx          = mesh_proxy->transform_;
                    map_ptr->mtx_cofactor = math::Mat34(math::Mat33::Cofactor(mesh_proxy->transform_.GetMat33()));  // 余因子行列.

                    mesh_instance_cbh->buffer_.Unmap();
                }

                const auto shape_count = model->NumShape();
                for (int shape_i = 0; shape_i < shape_count; ++shape_i)
                {
                    // Shapeに対応したMaterial Pass Psoを取得.
                    const auto&& pso = model->shape_mtl_pso_set_[shape_i].GetPassPso(pass_name);

                    // Descriptor.
                    {
                        ngl::rhi::DescriptorSetDep desc_set;

                        {
                            if (auto* p_view = render_mesh_resouce.cbv_sceneview.p_view)
                                pso->SetView(&desc_set, render_mesh_resouce.cbv_sceneview.slot_name.Get(), p_view);

                            if (auto* p_view = render_mesh_resouce.cbv_d_shadowview.p_view)
                                pso->SetView(&desc_set, render_mesh_resouce.cbv_d_shadowview.slot_name.Get(), p_view);
                        }

                        pso->SetView(&desc_set, "cb_ngl_instance", &mesh_instance_cbh->cbv_);

                        // モデルのマテリアル/モデル固有リソースのDescriptorSetの設定
                        BindModelResourceOptionCallbackArg bind_model_resource_option_callback_arg;
                        {
                            bind_model_resource_option_callback_arg.pso         = pso;
                            bind_model_resource_option_callback_arg.desc_set    = &desc_set;
                            bind_model_resource_option_callback_arg.shape_index = shape_i;
                        };
                        model->BindModelResourceCallback(bind_model_resource_option_callback_arg);

                        command_list.SetPipelineState(pso);
                        // DescriptorSetでViewを設定.
                        command_list.SetDescriptorSet(pso, &desc_set);
                    }

                    // Geometry.
                    model->DrawShape(&command_list, shape_i);
                }
            }
        }

    }  // namespace gfx
}  // namespace ngl

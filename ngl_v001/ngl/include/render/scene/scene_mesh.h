#pragma once

#include "framework/gfx_scene_entity_mesh.h"
#include "rhi/d3d12/command_list.d3d12.h"

namespace ngl::gfx::scene
{

    class SceneMesh
    {
    public:
        SceneMesh()
        {
        }
        ~SceneMesh()
        {
        }

        bool Initialize(rhi::DeviceDep* p_device, fwk::GfxScene* gfx_scene, const res::ResourceHandle<ResMeshData>& res_mesh, const char* material_name = "opaque_standard")
        {
            if (!gfx_mesh_entity_.Initialize(gfx_scene))
            {
                assert(false);
            }

            if (!model_.Initialize(p_device, res_mesh, material_name))
            {
                assert(false);
            }

            return true;
        }
        void Finalize()
        {
            // Proxyの解放.
            gfx_mesh_entity_.Finalize();
        }

        void SetTransform(const math::Mat34& mtx)
        {
            transform_ = mtx;
        }
        const math::Mat34& GetTransform() const
        {
            return transform_;
        }

        void UpdateGfx()
        {
            assert(fwk::GfxSceneEntityId::IsValid(gfx_mesh_entity_.proxy_info_.proxy_id_));
            assert(gfx_mesh_entity_.proxy_info_.scene_);

            // GfxScene上のSkyBox Proxyの情報を更新するRenderCommand. ProxyはIDによってRenderPassからアクセス可能で, SkyBoxの描画パラメータ送付に利用される.
            fwk::PushCommonRenderCommand([this](fwk::ComonRenderCommandArg arg)
                                         {
                // TODO. Entityが破棄されると即時Proxyが破棄されるため, 破棄フレームでもRenderThreadで安全にアクセスできるようにEntityの破棄リスト対応する必要がある.
                auto* proxy = gfx_mesh_entity_.GetProxy();
                assert(proxy);

                proxy->model_ = &model_;
                proxy->transform_ = transform_; });
        }
        fwk::GfxSceneEntityId GetMeshProxyId() const
        {
            // ProxyのID.
            return gfx_mesh_entity_.proxy_info_.proxy_id_;
        }

    public:
        StandardRenderModel* GetModel()
        {
            return &model_;
        }
        const StandardRenderModel* GetModel() const
        {
            return &model_;
        }
        // DrawShapeを独自の実装で置き換える.
        void SetProceduralDrawShapeFunc(const std::function<void(rhi::GraphicsCommandListDep*, int)>& func)
        {
            model_.procedural_draw_shape_func_ = func;
            
            /*
                // 例.
                scene_mesh->SetProceduralDrawShapeFunc(
                    [this](ngl::rhi::GraphicsCommandListDep* p_command_list, int shape_index)
                    { 
                        p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                        p_command_list->DrawInstanced(6, 1, 0, 0); 
                                            
                    });
            */
        }

    private:
        fwk::GfxSceneEntityMesh gfx_mesh_entity_;

    private:
        StandardRenderModel model_ = {};
        math::Mat34 transform_     = math::Mat34::Identity();
    };

}  // namespace ngl::gfx::scene

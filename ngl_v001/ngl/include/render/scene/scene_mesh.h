#pragma once

#include "framework/gfx_scene_entity_mesh.h"
#include "rhi/d3d12/command_list.d3d12.h"

namespace ngl::gfx::scene
{

    struct _SceneMeshGameUpdateCallbackArg
    {
        int dummy;
    };
    using SceneMeshGameUpdateCallbackArg = const _SceneMeshGameUpdateCallbackArg&;
    using SceneMeshGameUpdateCallback = std::function<void(SceneMeshGameUpdateCallbackArg)>;

    
    struct _SceneMeshRenderUpdateCallbackArg
    {
        rhi::GraphicsCommandListDep* command_list{};
    };
    using SceneMeshRenderUpdateCallbackArg = const _SceneMeshRenderUpdateCallbackArg&;
    using SceneMeshRenderUpdateCallback = std::function<void(SceneMeshRenderUpdateCallbackArg)>;


    class SceneMesh
    {
    public:
        SceneMesh()
        {
        }
        virtual ~SceneMesh()
        {
            Finalize();
        }


        bool Initialize(rhi::DeviceDep* p_device, fwk::GfxScene* gfx_scene, const res::ResourceHandle<ResMeshData>& res_mesh, std::shared_ptr<gfx::MeshData> override_mesh_shape_data = {}, const char* material_name = "opaque_standard")
        {
            if (!gfx_mesh_entity_.Initialize(gfx_scene))
            {
                assert(false);
            }

            if (!model_.Initialize(p_device, res_mesh, override_mesh_shape_data, material_name))
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

        // GameThread更新. Renderのための情報更新をする.
        void UpdateForRender()
        {
            assert(fwk::GfxSceneEntityId::IsValid(gfx_mesh_entity_.proxy_info_.proxy_id_));
            assert(gfx_mesh_entity_.proxy_info_.scene_);

            if(game_update_callback_)
            {
                // GameThread更新の追加処理.
                _SceneMeshGameUpdateCallbackArg arg;
                {
                    // TODO. GameThread更新の引数を必要に応じて設定.
                }
                game_update_callback_(arg);
            }

            // GfxScene上のSceneMesh Proxyの情報を更新するRenderCommandを登録. RenderThread実行されるGfxに安全に情報送付するためのもの.
            //  ProxyはIDによってRenderPassからアクセス可能で, GfxScene上のSceneMeshの描画パラメータ送付に利用される.
            fwk::PushCommonRenderCommand([this](fwk::ComonRenderCommandArg arg)
                                         {
                // TODO. Entityが破棄されると即時Proxyが破棄されるため, 破棄フレームでもRenderThreadで安全にアクセスできるようにEntityの破棄リスト対応する必要がある.
                auto* proxy = gfx_mesh_entity_.GetProxy();
                assert(proxy);

                if(render_update_callback_)
                {
                    // Callbackがあれば呼び出し. 呼び出されるCallback内でのGameThread-RenderThreadのDataRace対策はCallback実装者の責任.
                    _SceneMeshRenderUpdateCallbackArg arg_render_update;
                    {
                        arg_render_update.command_list = arg.command_list;
                    }
                    render_update_callback_(arg_render_update);
                }

                // gfx_meshのproxyに描画用の情報を設定.
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
        
        // Modelの追加リソースバインドコールバック関数を設定.
        void SetBindModelResourceOptionCallback(const BindModelResourceOptionCallback& func)
        {
            model_.bind_model_resource_option_callback_ = func;
            
            /*
                // 例.
                scene_mesh->SetModelResourceOptionCallback(
                    [this](BindModelResourceOptionCallbackArg arg)
                    { 
                        arg.pso->SetView(arg.desc_set, "optional_resource", model_optional_resource);
                    });
            */
        }

        
        // DrawShapeを独自の実装で置き換える.
        void SetProceduralDrawShapeFunc(const DrawShapeOverrideFuncion& func)
        {
            model_.draw_shape_override_ = func;
            
            /*
                // 例.
                scene_mesh->SetProceduralDrawShapeFunc(
                    [this](DrawShapeOverrideFuncionArg arg)
                    { 
                        arg.p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                        arg.p_command_list->DrawInstanced(6, 1, 0, 0); 
                                            
                    });
            */
        }

        // Game更新のコールバックを設定.
        // 基底クラスSceneMeshのGameThreaed更新時にコールバックされます.
        void SetGameUpdateCallback(const SceneMeshGameUpdateCallback& func)
        {
            game_update_callback_ = func;
            
            /*
                // 例.
                scene_mesh->SetGameUpdateCallback(
                    [this](SceneMeshGameUpdateCallbackArg arg)
                    { 
                        // GameThread更新の追加処理.
                    });
            */
        }
        // Rennder更新のコールバックを設定.
        // 基底クラスSceneMeshのRenderThreaed更新時にコールバックされます.
        void SetRenderUpdateCallback(const SceneMeshRenderUpdateCallback& func)
        {
            render_update_callback_ = func;
            
            /*
                // 例.
                scene_mesh->SetRenderUpdateCallback(
                    [this](SceneMeshRenderUpdateCallbackArg arg)
                    { 
                        // RenderThread更新の追加処理.
                    });
            */
        }
    private:
        fwk::GfxSceneEntityMesh gfx_mesh_entity_;

    private:
        StandardRenderModel model_ = {};
        math::Mat34 transform_     = math::Mat34::Identity();

        // GameTHreadでの更新処理に差し込むコールバック.
        SceneMeshGameUpdateCallback game_update_callback_{};
        // render threadでの更新処理に差し込むコールバック.
        SceneMeshRenderUpdateCallback render_update_callback_{};
    };

}  // namespace ngl::gfx::scene

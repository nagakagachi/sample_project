
#include "framework/gfx_scene.h"



namespace ngl::fwk
{


    bool GfxSkyBoxEntity::Initialize(GfxScene* scene)
    {
        const auto proxy_id = scene->buffer_skybox_.Alloc();
        assert(GfxSceneInstanceId::IsValid(proxy_id));

        scene_ = scene;
        proxy_id_ = proxy_id;
        return true;
    }
    void GfxSkyBoxEntity::Finalize()
    {
        if (GfxSceneInstanceId::IsValid(proxy_id_))
        {
            assert(scene_);
            assert(GfxSceneInstanceId::IsValid(proxy_id_));

            scene_->buffer_skybox_.Dealloc(proxy_id_);
            scene_ = {};
            proxy_id_ = {};
        }
    }
    GfxSkyBoxEntity::~GfxSkyBoxEntity()
    {
        Finalize();
    }

    


    IGfxSceneEntity::~IGfxSceneEntity()
    {
        assert(gfx_scene_);
        // 登録解除.
        gfx_scene_->UnregisterEntity(this);
    }
    

    void GfxScene::RegisterEntity(IGfxSceneEntity* entity)
    {
        {
            std::scoped_lock<std::mutex> lock(mutex_);

            // 登録ID検索.
            int empty_pos = -1;
            {
                for (int i = 0; 0 > empty_pos && i < component_db_.size(); ++i)
                {
                    if (nullptr == component_db_[i])
                    {
                        empty_pos = i;
                        break;
                    }
                }
                // 空きがないならバッファ拡張.
                if (0 > empty_pos)
                {
                    empty_pos = static_cast<int>(component_db_.size());
                    component_db_.push_back({});
                }
                assert(0 <= empty_pos && component_db_.size() > empty_pos);
            }

            // 登録.
            component_db_[empty_pos] = entity;
            {
                assert(!GfxSceneInstanceId::IsValid(entity->gfx_scene_entity_id_));
                assert(nullptr == entity->gfx_scene_);
                
                entity->gfx_scene_entity_id_ = GfxSceneInstanceId::Generate(static_cast<u32>(empty_pos));
                entity->gfx_scene_ = this;// 登録シーン保持.
            }
        }
    }
    void GfxScene::UnregisterEntity(IGfxSceneEntity* entity)
    {
        assert(entity);
        assert(entity->gfx_scene_ == this);

        u32 entity_index = entity->gfx_scene_entity_id_.GetIndex();
        {
            std::scoped_lock<std::mutex> lock(mutex_);

            assert(component_db_.size() > entity_index);
            assert(component_db_[entity_index] == entity);

            // 除去.
            component_db_[entity_index] = nullptr;
        }
    }
    
}
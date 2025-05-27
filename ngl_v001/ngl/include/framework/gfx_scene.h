#pragma once

#include <mutex>
#include <vector>

// core.
#include "gfx_scene_entity.h"

// implements.
#include "gfx_scene_skybox.h"

namespace ngl::fwk
{
    // ---------------------------------------------------------------------------------------------------
    class GfxScene
    {
    public:
        GfxScene() = default;
        /*
            EntityType毎に特殊化するためのベース宣言. GfxSceneProxyBuffer<T> 毎にメンバ変数宣言とそれを返す特殊化を定義することで, EntityType毎に別のメンバGfxSceneProxyBuffer上に実体確保できるようになる.
        example:
            GfxSceneProxyBuffer<GfxSkyBoxEntity> buffer_skybox_;
            template<> GfxSceneProxyBuffer<GfxSkyBoxEntity>* GetEntityProxyBuffer()
            {
                return &buffer_skybox_;
            }
        */
        template<typename ENTITY_TYPE, typename DUMMY = void>
        GfxSceneProxyBuffer<ENTITY_TYPE>* GetEntityProxyBuffer(){
            assert(false && u8"not implemented"); return {};
        }

        
    public:
        // ------------------------------------------------------------------------------------------
        // ------------------------------------------------------------------------------------------
        // Implement EntityType.

        // GfxSkyBoxEntity 用のバッファとメソッド特殊化定義.
        //  クラススコープでの完全特殊化はC++17以前ではコンパイルできない場合があるため別の記述方法を検討.
        GfxSceneProxyBuffer<GfxSkyBoxEntity> buffer_skybox_;
        template<> GfxSceneProxyBuffer<GfxSkyBoxEntity>* GetEntityProxyBuffer()
        {
            return &buffer_skybox_;
        }
        
        // TODO. other EntityTypes
        // GfxSceneProxyBuffer<Gfx***Entity> buffer_***_;


        
        // ------------------------------------------------------------------------------------------
        // ------------------------------------------------------------------------------------------
    public:
        // ENTITY_TYPEのバッファ上に確保, 解放. 実装は gfx_scene.inl .
        template<typename ENTITY_TYPE> GfxProxyInfo AllocProxy();
        template<typename ENTITY_TYPE> void DeallocProxy(GfxProxyInfo& proxy_info);
    };




    #include "gfx_scene.inl"




    // GfxScene
    // Entity別のBufferから確保するための. EntityType型別に特殊化された GetEntityProxyBuffer() に対して操作する.
    template<typename ENTITY_TYPE>
    GfxProxyInfo GfxScene::AllocProxy()
    {
        // GfxScene側で特殊化しているはずのEntity型のBufferを取得する.
        auto* entity_proxy_buffer = GetEntityProxyBuffer<ENTITY_TYPE>();

        const auto proxy_id = entity_proxy_buffer->Alloc();// 対応するTypeのBufferから確保.

        assert(GfxSceneEntityId::IsValid(proxy_id));
        GfxProxyInfo proxy_info{};
        {
            proxy_info.scene_ = this;
            proxy_info.proxy_id_ = proxy_id;
        }
        return proxy_info;
    }
    // Entity別のBufferに確保した要素を解放する. EntityType型別に特殊化された GetEntityProxyBuffer() に対して操作する.
    template<typename ENTITY_TYPE>
    void GfxScene::DeallocProxy(GfxProxyInfo& proxy_info)
    {
        // GfxScene側で特殊化しているはずのEntity型のBufferを取得する.
        auto* entity_proxy_buffer = GetEntityProxyBuffer<ENTITY_TYPE>();

        assert(proxy_info.scene_ == this);
        assert(GfxSceneEntityId::IsValid(proxy_info.proxy_id_));

        if (GfxSceneEntityId::IsValid(proxy_info.proxy_id_))
        {
            entity_proxy_buffer->Dealloc(proxy_info.proxy_id_);// 対応するTypeのBufferから確保.

            proxy_info = {};// 内容クリア.
        }
    }


    // -----------------------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------------------

    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::~GfxSceneEntityBase()
    {
        Finalize();
    }
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    bool GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::Initialize(class GfxScene* scene)
    {
        // 対応するProxyをSceneのBuffer上に確保.
        proxy_info_ = scene->AllocProxy<ENTITY_CLASS_TYPE>();

        return true;
    }
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    void GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::Finalize()
    {
        if (GfxSceneEntityId::IsValid(proxy_info_.proxy_id_))
        {
            // 確保していたProxyを解放.
            proxy_info_.scene_->DeallocProxy<ENTITY_CLASS_TYPE>(proxy_info_);
        }
    }
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    PROXY_CLASS_TYPE* GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::GetProxy()
    {
        assert(GfxSceneEntityId::IsValid(proxy_info_.proxy_id_));
        assert(nullptr != proxy_info_.scene_);
        if (GfxSceneEntityId::IsValid(proxy_info_.proxy_id_))
        {
            // GfxScene側で特殊化しているはずのEntity型のBufferを取得する.
            auto entity_proxy_buffer = proxy_info_.scene_->GetEntityProxyBuffer<ENTITY_CLASS_TYPE>();
            return entity_proxy_buffer->proxy_buffer_[proxy_info_.proxy_id_.GetIndex()];
        }
        return {};
    }


    // -----------------------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------------------


    // 初期化.
    template<typename ENTITY_TYPE>
    bool GfxSceneProxyBuffer<ENTITY_TYPE>::Initialize(u32 max_element_count)
    {
        proxy_buffer_.resize(max_element_count);
        std::fill(proxy_buffer_.begin(), proxy_buffer_.end(), nullptr);
        return true;
    }
    // Entity&Proxy確保.
    template<typename ENTITY_TYPE>
    GfxSceneEntityId GfxSceneProxyBuffer<ENTITY_TYPE>::Alloc()
    {
        {
            std::scoped_lock<std::mutex> lock(mutex_);

            // 空き要素検索.
            int register_location = -1;
            {
                for (int i = 0; 0 > register_location && i < proxy_buffer_.size(); ++i)
                {
                    if (nullptr == proxy_buffer_[i])
                    {
                        register_location = i;
                        break;
                    }
                }

                assert(0 <= register_location);
                if (0 > register_location)
                {
                    return {};
                }
            }

            assert(0 <= register_location && proxy_buffer_.size() > register_location);

            // 新規生成と登録.
            proxy_buffer_[register_location] = new typename ENTITY_TYPE::ProxyType();

            // Alloc情報をエンコードして返却.
            return GfxSceneEntityId::Generate(static_cast<u32>(register_location));
        }
    }
    // Entity&Proxy解放.
    template<typename ENTITY_TYPE>
    void GfxSceneProxyBuffer<ENTITY_TYPE>::Dealloc(GfxSceneEntityId id)
    {
        assert(GfxSceneEntityId::IsValid(id));

        const auto index = id.GetIndex();
        assert(proxy_buffer_.size() > index);

        assert(nullptr != proxy_buffer_[index]);

        {
            std::scoped_lock<std::mutex> lock(mutex_);

            // 破棄 & クリア.
            delete proxy_buffer_[index];
            proxy_buffer_[index] = nullptr;
        }
    }
    
}




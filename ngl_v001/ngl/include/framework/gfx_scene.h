#pragma once

#include <mutex>
#include <vector>

// core.
#include "gfx_scene_entity.h"


// implements.
#include "gfx_scene_skybox.h"


namespace ngl::fwk
{
    class GfxScene; 

    
    // ---------------------------------------------------------------------------------------------------
    class GfxScene
    {
        // 一旦Public
    public:
        // EntityType毎に特殊化し, それぞれの型のRegisterへのポインタを返すように実装すること.
        template<typename ENTITY_TYPE>
        GfxSceneProxyRegister<ENTITY_TYPE>* GetEntityTypeRegister()
        {
            assert(false && u8"not implemented");
            return {};
        }

        // Entity別のRegisterから確保するための. EntityType型別に特殊化された GetEntityTypeRegister() に対して操作する.
        template<typename ENTITY_TYPE>
        GfxProxyInfo AllocProxy()
        {
            auto* entity_register = GetEntityTypeRegister<ENTITY_TYPE>();

            const auto proxy_id = entity_register->Alloc();// 対応するTypeのBufferから確保.

            assert(GfxSceneEntityId::IsValid(proxy_id));
            GfxProxyInfo proxy_info{};
            {
                proxy_info.scene_ = this;
                proxy_info.proxy_id_ = proxy_id;
            }
            return proxy_info;
        }
        // Entity別のRegisterに確保した要素を解放する. EntityType型別に特殊化された GetEntityTypeRegister() に対して操作する.
        template<typename ENTITY_TYPE>
        void DeallocProxy(GfxProxyInfo& proxy_info)
        {
            auto* entity_register = GetEntityTypeRegister<ENTITY_TYPE>();

            assert(proxy_info.scene_ == this);
            assert(GfxSceneEntityId::IsValid(proxy_info.proxy_id_));

            if (GfxSceneEntityId::IsValid(proxy_info.proxy_id_))
            {
                entity_register->Dealloc(proxy_info.proxy_id_);// 対応するTypeのBufferから確保.

                proxy_info = {};// 内容クリア.
            }
        }




        // GfxSkyBoxEntity用のバッファとメソッド特殊化定義.
        //friend GfxSkyBoxEntity;
        GfxSceneProxyRegister<GfxSkyBoxEntity> buffer_skybox_;
        template<> GfxSceneProxyRegister<GfxSkyBoxEntity>* GetEntityTypeRegister()
        {
            return &buffer_skybox_;
        }

    };




    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::~GfxSceneEntityBase()
    {
        Finalize();
    }
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    bool GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::Initialize(class GfxScene* scene)
    {
        proxy_info_ = scene->AllocProxy<ENTITY_CLASS_TYPE>();

        return true;
    }
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    void GfxSceneEntityBase<ENTITY_CLASS_TYPE, PROXY_CLASS_TYPE>::Finalize()
    {
        if (GfxSceneEntityId::IsValid(proxy_info_.proxy_id_))
        {
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
            auto proxy_register = proxy_info_.scene_->GetEntityTypeRegister<ENTITY_CLASS_TYPE>();
            return proxy_register->proxy_buffer_[proxy_info_.proxy_id_.GetIndex()];
        }
        return {};
    }


    


    
    
}
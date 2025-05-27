#pragma once
    // gfx_scene.h の末尾でincludeされる. 単にtemplateクラスメソッドの定義を後付するため.


// -----------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------

    // GfxScene
    // Entity別のRegisterから確保するための. EntityType型別に特殊化された GetEntityTypeRegister() に対して操作する.
    template<typename ENTITY_TYPE>
    GfxProxyInfo GfxScene::AllocProxy()
    {
        // GfxScene側で特殊化しているはずのEntity型のRegisterを取得する.
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
    void GfxScene::DeallocProxy(GfxProxyInfo& proxy_info)
    {
        // GfxScene側で特殊化しているはずのEntity型のRegisterを取得する.
        auto* entity_register = GetEntityTypeRegister<ENTITY_TYPE>();

        assert(proxy_info.scene_ == this);
        assert(GfxSceneEntityId::IsValid(proxy_info.proxy_id_));

        if (GfxSceneEntityId::IsValid(proxy_info.proxy_id_))
        {
            entity_register->Dealloc(proxy_info.proxy_id_);// 対応するTypeのBufferから確保.

            proxy_info = {};// 内容クリア.
        }
    }


// -----------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------

    // GfxSceneEntityBase
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
            // GfxScene側で特殊化しているはずのEntity型のRegisterを取得する.
            auto proxy_register = proxy_info_.scene_->GetEntityTypeRegister<ENTITY_CLASS_TYPE>();
            return proxy_register->proxy_buffer_[proxy_info_.proxy_id_.GetIndex()];
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
// -----------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------
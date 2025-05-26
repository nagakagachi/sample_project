#pragma once

#include <mutex>
#include <vector>
#include <assert.h>

#include "util/types.h"
#include "util/noncopyable.h"

namespace ngl::fwk
{
    class GfxScene;
    

    // ---------------------------------------------------------------------------------------------------
    struct GfxSceneEntityId
    {
        using DataType = u32;
        DataType data{};

        static void EncodeData(DataType& out_data, u32 index)
        {
            // 0は無効値であるので +1 した値を格納.
            out_data = index + 1;
        }
        static void DecodeData(const DataType& data, u32& out_index)
        {
            assert(0 < data);
            // +1した値からインデックスを復元.
            out_index = data - 1;
        }

        // パラメータからIDインスタンス生成.
        static GfxSceneEntityId Generate(u32 index)
        {
            GfxSceneEntityId id{};
            {
                EncodeData(id.data, index);
            }
            return id;
        }

        u32 GetIndex() const
        {
            assert(IsValid(*this));
            u32 index;
            DecodeData(this->data, index);
            return index;
        }
        
        static bool IsValid(const GfxSceneEntityId& v)
        {
            // 0 は無効値. 初期化の簡易化のため.
            return 0 != v.data;
        }
    };


    struct GfxProxyInfo
    {
        GfxScene*           scene_{};       // 登録先Scene.
        GfxSceneEntityId    proxy_id_ = {}; // Proxyへの参照用.
    };


    // ENTITY_TYPE : Proxyを持つEntityクラスタイプ.
    //  EntityのProxyの確保登録と解放を担当.
    //  内部バッファにスレッドセーフに登録/解除.
    template<typename ENTITY_TYPE>
    class GfxSceneProxyRegister
    {
    public:
        bool Initialize(u32 max_element_count);

        GfxSceneEntityId Alloc();
        void Dealloc(GfxSceneEntityId id);

        std::mutex mutex_;
        std::vector<typename ENTITY_TYPE::ProxyType*> proxy_buffer_{};
    };
    

    // 初期化.
    template<typename ENTITY_TYPE>
    bool GfxSceneProxyRegister<ENTITY_TYPE>::Initialize(u32 max_element_count)
    {
        proxy_buffer_.resize(max_element_count);
        std::fill(proxy_buffer_.begin(), proxy_buffer_.end(), nullptr);
        return true;
    }
    // Entity&Proxy確保.
    template<typename ENTITY_TYPE>
    GfxSceneEntityId GfxSceneProxyRegister<ENTITY_TYPE>::Alloc()
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
    void GfxSceneProxyRegister<ENTITY_TYPE>::Dealloc(GfxSceneEntityId id)
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


    // GfxSceneEntityの基本設定と機能を提供する基底クラス.
    //  派生クラスではoverride不要で, この基底クラスの Initialize, Finalize, GetProxy でGfxScene上のProxyインスタンスを操作できる.
    template<typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    class GfxSceneEntityBase
    {
    public:
        using EntityType = ENTITY_CLASS_TYPE;
        using ProxyType = PROXY_CLASS_TYPE;

        GfxSceneEntityBase() = default;
        virtual ~GfxSceneEntityBase();

        // GfxSceneEntityの初期化. GfxScene上にProxyを生成.
        bool Initialize(class GfxScene* scene);

        // GfxSceneEntityの解放. GfxScene上に確保したProxyの破棄.
        void Finalize();
        
        // 確保しているProxyを取得.
        PROXY_CLASS_TYPE* GetProxy();


        GfxProxyInfo    proxy_info_{};
    };

}

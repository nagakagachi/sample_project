/*
    gfx_scene_entity.h
*/
#pragma once

#include <assert.h>

#include <mutex>
#include <vector>

#include "util/types.h"

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
        GfxScene* scene_{};               // 登録先Scene.
        GfxSceneEntityId proxy_id_ = {};  // Proxyへの参照用.
    };

    // ENTITY_TYPE : Proxyを持つEntityクラスタイプ.
    //  EntityのProxyの確保登録と解放を担当. GfxSceneがメンバとして各タイプバージョンをもつ.
    //  内部バッファにスレッドセーフに登録/解除.
    //  実装は gfx_scene.inl
    template <typename ENTITY_TYPE>
    class GfxSceneProxyBuffer
    {
    public:
        bool Initialize(u32 max_element_count);

        GfxSceneEntityId Alloc();
        void Dealloc(GfxSceneEntityId id);

        std::mutex mutex_;
        std::vector<typename ENTITY_TYPE::ProxyType*> proxy_buffer_{};
    };

    // GfxSceneEntityの基本設定と機能を提供する基底クラス.
    //  派生クラスではoverride不要で, この基底クラスの Initialize, Finalize, GetProxy でGfxScene上のProxyインスタンスを操作できる.
    //  実装は gfx_scene.inl
    template <typename ENTITY_CLASS_TYPE, typename PROXY_CLASS_TYPE>
    class GfxSceneEntityBase
    {
    public:
        using EntityType = ENTITY_CLASS_TYPE;
        using ProxyType  = PROXY_CLASS_TYPE;

        GfxSceneEntityBase() = default;
        virtual ~GfxSceneEntityBase();

        // GfxSceneEntityの初期化. GfxScene上にProxyを生成.
        bool Initialize(class GfxScene* scene);

        // GfxSceneEntityの解放. GfxScene上に確保したProxyの破棄.
        void Finalize();

        // 確保しているProxyを取得.
        PROXY_CLASS_TYPE* GetProxy();

        GfxProxyInfo proxy_info_{};
    };

}  // namespace ngl::fwk

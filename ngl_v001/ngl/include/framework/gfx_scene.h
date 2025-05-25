#pragma once

#include <mutex>
#include <vector>

#include "rhi/d3d12/resource_view.d3d12.h"
#include "resource/resource_manager.h"

namespace ngl::fwk
{

    

    

    // ---------------------------------------------------------------------------------------------------
    struct GfxSceneInstanceId
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
        static GfxSceneInstanceId Generate(u32 index)
        {
            GfxSceneInstanceId id{};
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
        
        static bool IsValid(const GfxSceneInstanceId& v)
        {
            // 0 は無効値. 初期化の簡易化のため.
            return 0 != v.data;
        }
    };

    
    // ---------------------------------------------------------------------------------------------------
    // IGfxSceneEntity 派生クラスのスコープ内で宣言.
    #define NGL_GFX_SCENE_ENTITY_DECLARE(CLASS) \
    public:\
    /*GfxSceneEntityクラスタイプID.*/ \
    static u32 gfx_scene_entity_type_id; \
    CLASS() = default;

    // IGfxSceneEntity 派生クラスの実装として定義.
    #define NGL_GFX_SCENE_ENTITY_DEFINE(CLASS) u32 CLASS::gfx_scene_entity_type_id = ngl::fwk::GfxScene::RegisterEntityType<CLASS>();
    
    // ---------------------------------------------------------------------------------------------------
    // 派生クラスは以下の手順が必要.
    //  クラススコープ内で NGL_GFX_SCENE_ENTITY_DECLARE 記述
    //  クラスcpp内で NGL_GFX_SCENE_ENTITY_DEFINE 記述
    class IGfxSceneEntity : public NonCopyableTp<IGfxSceneEntity>
    {
        friend class GfxScene;

        NGL_GFX_SCENE_ENTITY_DECLARE(IGfxSceneEntity)
        
    public:
        virtual  ~IGfxSceneEntity();

    private:
        GfxScene*        gfx_scene_{};
        GfxSceneInstanceId gfx_scene_entity_id_ = {};
    };



    //
    // GfxSkyBoxEntity の Proxy.
    class GfxSkyBoxProxy
    {
    public:
        // HDR Sky Panorama Texture.
        rhi::RefTextureDep panorama_texture_;
        rhi::RefSrvDep panorama_texture_srv_;

        // Mipmap有りのSky Cubemap. Panoramaイメージから生成される.
        rhi::RefTextureDep src_cubemap_;
        rhi::RefSrvDep src_cubemap_plane_array_srv_;

        // Sky Cubemapから畳み込みで生成されるDiffuse IBL Cubemap.
        rhi::RefTextureDep ibl_diffuse_cubemap_;
        rhi::RefSrvDep ibl_diffuse_cubemap_plane_array_srv_;
        
        // Sky Cubemapから畳み込みで生成されるGGX Specular IBL Cubemap.
        rhi::RefTextureDep ibl_ggx_specular_cubemap_;
        rhi::RefSrvDep ibl_ggx_specular_cubemap_plane_array_srv_;

        // IBL DFG LUT.
        rhi::RefTextureDep ibl_ggx_dfg_lut_;
        rhi::RefSrvDep ibl_ggx_dfg_lut_srv_;
    };
    //
    // SkyBoxのEntity.
    class GfxSkyBoxEntity : public NonCopyableTp<GfxSkyBoxEntity>
    {
    public:
        using EntityType = GfxSkyBoxEntity;
        // GfxScene上で確保されるProxyの型.
        using ProxyType = GfxSkyBoxProxy;

    public:
        bool Initialize(GfxScene* scene);
        void Finalize();
        ~GfxSkyBoxEntity();

        
        GfxScene*           scene_{};       // 登録先Scene.
        GfxSceneInstanceId  proxy_id_ = {}; // Proxyへの参照用.
    };

    
    

    // ENTITY_TYPE : Proxyを持つEntityクラスタイプ.
    //  EntityのProxyの確保登録と解放を担当.
    //  内部バッファにスレッドセーフに登録/解除.
    template<typename ENTITY_TYPE>
    class GfxSceneComponentRegister
    {
    public:
        bool Initialize(u32 max_element_count);

        GfxSceneInstanceId Alloc();
        void Dealloc(GfxSceneInstanceId id);

        std::mutex mutex_;
        std::vector<typename ENTITY_TYPE::ProxyType*> proxy_buffer_{};
    };
    
    // 初期化.
    template<typename ENTITY_TYPE>
    bool GfxSceneComponentRegister<ENTITY_TYPE>::Initialize(u32 max_element_count)
    {
        proxy_buffer_.resize(max_element_count);
        std::fill(proxy_buffer_.begin(), proxy_buffer_.end(), nullptr);
        return true;
    }
    // Proxy確保.
    template<typename ENTITY_TYPE>
    GfxSceneInstanceId GfxSceneComponentRegister<ENTITY_TYPE>::Alloc()
    {
        {
            std::scoped_lock<std::mutex> lock(mutex_);

            // 空き要素検索.
            int empty_pos = -1;
            {
                for (int i = 0; 0 > empty_pos && i < proxy_buffer_.size(); ++i)
                {
                    if (nullptr == proxy_buffer_[i])
                    {
                        empty_pos = i;
                        break;
                    }
                }

                assert(0 <= empty_pos);
                if (0 > empty_pos)
                {
                    return {};
                }
            }

            assert(0 <= empty_pos && proxy_buffer_.size() > empty_pos);

            // EntityのProxyTypeを新規生成して登録.
            proxy_buffer_[empty_pos] = new typename ENTITY_TYPE::ProxyType();

            // Alloc情報をエンコードして返却.
            return GfxSceneInstanceId::Generate(static_cast<u32>(empty_pos));
        }
    }
    // Proxy解放.
    template<typename ENTITY_TYPE>
    void GfxSceneComponentRegister<ENTITY_TYPE>::Dealloc(GfxSceneInstanceId id)
    {
        assert(GfxSceneInstanceId::IsValid(id));

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





    
    // ---------------------------------------------------------------------------------------------------
    class GfxScene
    {
        // 一旦Public
    public:
        friend GfxSkyBoxEntity;
        GfxSceneComponentRegister<GfxSkyBoxEntity> buffer_skybox_;

        
    public:
        // Entity生成.
        template<class ENTITY_TYPE>
        ENTITY_TYPE* NewEntity()
        {
            static_assert(std::is_base_of<IGfxSceneEntity, ENTITY_TYPE>::value);

            auto* entity = new ENTITY_TYPE();
            // 登録処理.
            RegisterEntity(entity);
            return entity;
        }
        
    public:
        void RegisterEntity(IGfxSceneEntity* entity);
        void UnregisterEntity(IGfxSceneEntity* entity);

    private:
        std::mutex mutex_;
        std::vector<IGfxSceneEntity*> component_db_{};// IGfxSceneEntity派生クラスのインスタンスが登録されれる.


    };


    


    
    
}
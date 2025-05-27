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
            template<> GfxSceneProxyBuffer<GfxSkyBoxEntity>* GetEntityTypeRegister()
            {
                return &buffer_skybox_;
            }
        */
        template<typename ENTITY_TYPE, typename DUMMY = void>
        GfxSceneProxyBuffer<ENTITY_TYPE>* GetEntityTypeRegister(){
            assert(false && u8"not implemented"); return {};
        }

        
    public:
        // ------------------------------------------------------------------------------------------
        // ------------------------------------------------------------------------------------------
        // Implement EntityType.

        // GfxSkyBoxEntity 用のバッファとメソッド特殊化定義.
        //  クラススコープでの完全特殊化はC++17以前ではコンパイルできない場合があるため別の記述方法を検討.
        GfxSceneProxyBuffer<GfxSkyBoxEntity> buffer_skybox_;
        template<> GfxSceneProxyBuffer<GfxSkyBoxEntity>* GetEntityTypeRegister()
        {
            return &buffer_skybox_;
        }
        
        // TODO. other EntityTypes
        // GfxSceneProxyBuffer<Gfx***Entity> buffer_skybox_;


        
        // ------------------------------------------------------------------------------------------
        // ------------------------------------------------------------------------------------------
    public:
        // ENTITY_TYPEのバッファ上に確保, 解放. 実装は gfx_scene.inl .
        template<typename ENTITY_TYPE> GfxProxyInfo AllocProxy();
        template<typename ENTITY_TYPE> void DeallocProxy(GfxProxyInfo& proxy_info);
    };

    #include "gfx_scene.inl"
    
}




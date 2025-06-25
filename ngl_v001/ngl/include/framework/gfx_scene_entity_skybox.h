#pragma once

#include "gfx_scene_entity.h"

#include "rhi/d3d12/resource_view.d3d12.h"
#include "resource/resource_manager.h"



namespace ngl::fwk
{
    //
    // GfxSceneEntitySkyBox の Proxy.
    //  Entityに対応してGfxSceneに確保され, アクセス用のインデックスで操作する.
    //  RenderCommandを利用してRenderThread上で操作され, RenderPassが参照する情報.
    class GfxSceneEntitySkyBoxProxy
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
    class GfxSceneEntitySkyBox : public GfxSceneEntityBase<GfxSceneEntitySkyBox, GfxSceneEntitySkyBoxProxy>
    {
    public:
    };


} // namespace ngl::fwk
﻿#pragma once


#include "gfx/material/material_shader_manager.h"
#include "gfx/resource/resource_mesh.h"
#include "gfx/resource/resource_texture.h"
#include "math/math.h"
#include "resource/resource.h"

namespace ngl
{
namespace gfx
{
    class StandardRenderMaterial
    {
    public:
        res::ResourceHandle<ResTexture> tex_basecolor = {};
        res::ResourceHandle<ResTexture> tex_normal = {};
        res::ResourceHandle<ResTexture> tex_occlusion = {};
        res::ResourceHandle<ResTexture> tex_roughness = {};
        res::ResourceHandle<ResTexture> tex_metalness = {};
    };
    
    class StandardRenderModel
    {
    public:
        bool Initialize(rhi::DeviceDep* p_device, res::ResourceHandle<ResMeshData> res_mesh);
        
    public:
        res::ResourceHandle<ResMeshData> res_mesh_ = {};
        std::vector<StandardRenderMaterial> material_array_ = {};
        std::vector<MaterialPsoSet> shape_mtl_pso_set_ = {};
    };
    
}
}

/*
    standard_render_model.h
*/
#pragma once

#include "gfx/material/material_shader_manager.h"
#include "gfx/resource/resource_mesh.h"
#include "gfx/resource/resource_texture.h"
#include "math/math.h"
#include "text/hash_text.h"
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
        StandardRenderModel() = default;
        ~StandardRenderModel() = default;

        bool Initialize(rhi::DeviceDep* p_device, res::ResourceHandle<ResMeshData> res_mesh, const char* material_name);
        
        // Descriptorへのリソース設定コールバック.        
        void BindModelResourceCallback(rhi::GraphicsPipelineStateDep* pso, rhi::DescriptorSetDep* desc_set, int shape_index);

        void DrawShape(rhi::GraphicsCommandListDep* p_command_list, int shape_index);

    public:
        res::ResourceHandle<ResMeshData> res_mesh_ = {};
        std::vector<MaterialPsoSet> shape_mtl_pso_set_ = {};
        std::vector<StandardRenderMaterial> material_array_ = {};

        std::function<void(rhi::GraphicsPipelineStateDep* pso, rhi::DescriptorSetDep* desc_set, int shape_index)> bind_model_resource_option_callback_{};
        std::function<void(rhi::GraphicsCommandListDep* command_list, int shape_index)> draw_shape_override_{};
    };
}
}

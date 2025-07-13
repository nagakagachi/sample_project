/*
    standard_render_model.h
*/
#pragma once

#include "gfx/material/material_shader_manager.h"
#include "gfx/resource/resource_mesh.h"
#include "gfx/resource/resource_texture.h"
#include "math/math.h"
#include "resource/resource.h"
#include "text/hash_text.h"

namespace ngl
{
    namespace gfx
    {

        // コールバック定義簡易化用定義.
        struct _BindModelResourceOptionCallbackArg
        {
            rhi::GraphicsPipelineStateDep* pso;
            rhi::DescriptorSetDep* desc_set;
            int shape_index;
        };
        using BindModelResourceOptionCallbackArg = const _BindModelResourceOptionCallbackArg&;
        using BindModelResourceOptionCallback    = std::function<void(BindModelResourceOptionCallbackArg)>;

        // コールバック定義簡易化用定義.
        struct _DrawShapeOverrideFuncionArg
        {
            rhi::GraphicsCommandListDep* command_list;
            int shape_index;
        };
        using DrawShapeOverrideFuncionArg = const _DrawShapeOverrideFuncionArg&;
        using DrawShapeOverrideFuncion    = std::function<void(DrawShapeOverrideFuncionArg)>;

        class StandardRenderMaterial
        {
        public:
            res::ResourceHandle<ResTexture> tex_basecolor = {};
            res::ResourceHandle<ResTexture> tex_normal    = {};
            res::ResourceHandle<ResTexture> tex_occlusion = {};
            res::ResourceHandle<ResTexture> tex_roughness = {};
            res::ResourceHandle<ResTexture> tex_metalness = {};
        };

        class StandardRenderModel
        {
        public:
            StandardRenderModel()  = default;
            ~StandardRenderModel() = default;

            bool Initialize(rhi::DeviceDep* p_device, res::ResourceHandle<ResMeshData> res_mesh, const char* material_name);

            // Descriptorへのリソース設定コールバック.
            void BindModelResourceCallback(BindModelResourceOptionCallbackArg arg);

            void DrawShape(rhi::GraphicsCommandListDep* p_command_list, int shape_index);

        public:
            res::ResourceHandle<ResMeshData> res_mesh_          = {};
            std::vector<MaterialPsoSet> shape_mtl_pso_set_      = {};
            std::vector<StandardRenderMaterial> material_array_ = {};

            BindModelResourceOptionCallback bind_model_resource_option_callback_{};
            DrawShapeOverrideFuncion draw_shape_override_{};
        };
    }  // namespace gfx
}  // namespace ngl

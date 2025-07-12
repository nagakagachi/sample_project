/*
    mesh_renderer.h
*/
#pragma once

#include "math/math.h"

#include "framework/gfx_scene.h"

namespace ngl
{
namespace rhi
{
    class GraphicsCommandListDep;
}

namespace gfx
{
    template<typename ViewType>
    struct RenderMeshTemplate
    {
        rhi::ResourceViewName   slot_name = {};
        ViewType*               p_view = {};
    };
    using RenderMeshCbv = RenderMeshTemplate<rhi::ConstantBufferViewDep>;
    using RenderMeshSrv = RenderMeshTemplate<rhi::ShaderResourceViewDep>;
    using RenderMeshUav = RenderMeshTemplate<rhi::UnorderedAccessViewDep>;
    using RenderMeshSampler = RenderMeshTemplate<rhi::SamplerDep>;
    
    struct RenderMeshResource
    {
        RenderMeshCbv cbv_sceneview = {};// SceneView定数バッファ.
        
        RenderMeshCbv cbv_d_shadowview = {};// DirectionalShadowView定数バッファ.
    };
    
    void RenderMeshWithMaterial(
        rhi::GraphicsCommandListDep& command_list, const char* pass_name,
        fwk::GfxScene* gfx_scene, const std::vector<fwk::GfxSceneEntityId>& mesh_proxy_id_array, const RenderMeshResource& render_mesh_resouce);
}
}

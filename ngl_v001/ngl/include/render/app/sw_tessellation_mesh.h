/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "render/app/half_edge_mesh.h"
#include "render/app/concurrent_binary_tree.h"
#include "render/scene/scene_mesh.h"

namespace ngl::render::app
{
    class SwTessellationMesh : public ngl::gfx::scene::SceneMesh
    {
        using Super = ngl::gfx::scene::SceneMesh;

    public:
        SwTessellationMesh() = default;
        ~SwTessellationMesh();

        // 初期化
        bool Initialize(
            ngl::rhi::DeviceDep* p_device,
            ngl::fwk::GfxScene* gfx_scene,
            const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh);

    private:
        // Game更新.
        void UpdateOnGame(gfx::scene::SceneMeshGameUpdateCallbackArg arg);
        // Render更新.
        void UpdateOnRender(gfx::scene::SceneMeshRenderUpdateCallbackArg arg);

    private:
        std::vector<HalfEdgeMesh> half_edge_mesh_array_;

        std::vector<rhi::RefBufferDep> half_edge_buffer_array_;
        std::vector<rhi::RefSrvDep> half_edge_srv_array_;
    };

}  // namespace ngl::render::app

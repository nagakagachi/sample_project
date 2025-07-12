/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "render/scene/scene_mesh.h"

namespace ngl::render::app
{

    // HalfEdge構造体の定義.
    //  今回はTessellation用のためリンクとVertexIndexのみ.
    struct HalfEdge
    {
        int twin   = -1;
        int next   = -1;  // 次のエッジ
        int prev   = -1;  // 前のエッジ
        int vertex = -1;  // このエッジの頂点
    };

    // IndexListからHalfEdge構造を生成するクラス定義
    class HalfEdgeMesh
    {
    public:
        HalfEdgeMesh()  = default;
        ~HalfEdgeMesh() = default;

        // IndexListからHalfEdge構造を生成する関数
        void Initialize(const uint32_t* index_list, int index_count);

        std::vector<HalfEdge> half_edge_;
    };

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
        std::vector<HalfEdgeMesh> half_edge_mesh_array_;

        std::vector<rhi::RefBufferDep> half_edge_buffer_array_;
        std::vector<rhi::RefSrvDep> half_edge_srv_array_;
    };

}  // namespace ngl::render::app

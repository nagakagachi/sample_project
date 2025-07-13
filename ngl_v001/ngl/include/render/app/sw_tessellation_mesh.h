/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "util/bit_operation.h"
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


    struct Bisector
    {
        u32 bs_depth;
        u32 bs_index;

        int next;
        int prev;
        int twin;

        u32 command;
        u32 alloc_ptr[4];
    };
    static constexpr auto sizeof_Bisector = sizeof(Bisector);


    // 完全二分木ビットフィールド.
    class ConcurrentBinaryTreeU32
    {
        using LeafType = u32;
        static constexpr u32 LeafTypeBitWidth = sizeof(LeafType) * 8;
        static constexpr u32 LeafTypePackedIndexShift = ngl::MostSignificantBit32(LeafTypeBitWidth);
        static constexpr u32 LeafTypeBitLocalIndexMask = (1u << LeafTypePackedIndexShift) - 1;
        static constexpr u32 SumValueLocation = 1;
    public:
        ConcurrentBinaryTreeU32()  = default;
        ~ConcurrentBinaryTreeU32() = default;

        void Initialize(u32 require_leaf_count);

        void Clear();

        void SetBit(u32 index, u32 bit);
        u32 GetBit(u32 index) const;

        u32 GetSum() const;
        void SumReduction();

        // 下位から i番目 の 1 の位置を検索. SumReduction後に使用可能.
        int Find_ith_Bit1(u32 i);
        // 下位から i番目 の 0 の位置を検索. SumReduction後に使用可能.
        int Find_ith_Bit0(u32 i);

        u32 NumLeaf() const;
    
    public:
        static void Test();
    private:
        std::vector<u32> cbt_node_{};
        u32 packed_leaf_count_{};
        u32 packed_leaf_offset_{};
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

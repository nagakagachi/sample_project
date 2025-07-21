/*
    half_edge_mesh.h
    HalfEdge構造とBisector構造の定義
*/

#pragma once

#include <vector>
#include <cstdint>

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

    // Bisector構造体（テッセレーション用）
    // 16byteアライメント最適化済み
    struct Bisector
    {
        uint32_t bs_depth;      // 4 bytes
        uint32_t bs_id;         // 4 bytes
        uint32_t command;       // 4 bytes
        int      next;          // 4 bytes  (16 bytes total)
        
        int      prev;          // 4 bytes
        int      twin;          // 4 bytes
        int      alloc_ptr[4];  // 16 bytes  (24 bytes total)
    };
    static constexpr auto sizeof_Bisector = sizeof(Bisector);

}  // namespace ngl::render::app

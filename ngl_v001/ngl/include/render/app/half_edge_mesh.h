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
    struct Bisector
    {
        uint32_t bs_depth;
        uint32_t bs_index;

        int next;
        int prev;
        int twin;

        uint32_t command;
        uint32_t alloc_ptr[4];
    };
    static constexpr auto sizeof_Bisector = sizeof(Bisector);

}  // namespace ngl::render::app

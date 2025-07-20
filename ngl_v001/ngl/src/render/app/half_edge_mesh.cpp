/*
    half_edge_mesh.cpp
    HalfEdge構造とBisector構造の実装
*/

#include "render/app/half_edge_mesh.h"
#include <unordered_map>

namespace ngl::render::app
{
    // IndexListからHalfEdge構造を生成する関数
    void HalfEdgeMesh::Initialize(const uint32_t* index_list, int index_count)
    {
        // 三角形メッシュを仮定
        int face_count = index_count / 3;
        half_edge_.clear();
        half_edge_.reserve(index_count);

        // 各エッジを一意に識別するためのキー
        using EdgeKey    = uint64_t;
        auto MakeEdgeKey = [](int v0, int v1) -> EdgeKey
        {
            return (static_cast<uint64_t>(v0) << 32) | static_cast<uint32_t>(v1);
        };
        auto DecodeEdgeKey = [](EdgeKey key) -> std::pair<int, int>
        {
            int v0 = static_cast<int>(key >> 32);
            int v1 = static_cast<int>(key & 0xFFFFFFFF);
            return {v0, v1};
        };

        {
            std::unordered_map<EdgeKey, int> edge_map;
            // HalfEdge生成
            for (int f = 0; f < face_count; ++f)
            {
                int idx0 = index_list[f * 3 + 0];
                int idx1 = index_list[f * 3 + 1];
                int idx2 = index_list[f * 3 + 2];

                int he_base = static_cast<int>(half_edge_.size());
                // Triangle単位でHalfEdgeループを追加. twinは未定として後処理.
                half_edge_.push_back({-1, he_base + 1, he_base + 2, idx0});  // 0→1
                half_edge_.push_back({-1, he_base + 2, he_base + 0, idx1});  // 1→2
                half_edge_.push_back({-1, he_base + 0, he_base + 1, idx2});  // 2→0

                // エッジ情報をmapに登録
                edge_map[MakeEdgeKey(idx0, idx1)] = he_base + 0;
                edge_map[MakeEdgeKey(idx1, idx2)] = he_base + 1;
                edge_map[MakeEdgeKey(idx2, idx0)] = he_base + 2;
            }

            // twin探索
            for (const auto& [key, he_idx] : edge_map)
            {
                auto [v0, v1]    = DecodeEdgeKey(key);
                EdgeKey twin_key = MakeEdgeKey(v1, v0);
                auto it          = edge_map.find(twin_key);
                if (it != edge_map.end())
                {
                    half_edge_[he_idx].twin = it->second;
                }
            }
            edge_map.clear();
        }
    }

}  // namespace ngl::render::app

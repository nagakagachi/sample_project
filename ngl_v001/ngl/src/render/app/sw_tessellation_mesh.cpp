#include "render/app/sw_tessellation_mesh.h"


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



    void ConcurrentBinaryTreeU32::Initialize(u32 require_leaf_count)
    {
        // 以上の最小の2の冪
        const u32 min_large_power_of_2 = 1u << (ngl::MostSignificantBit32(require_leaf_count-1)+1);
        // require_leaf_countのリーフの完全二分木のノードを格納するためのサイズ.   
        const auto required_packed_leaf_count = std::max(1u, min_large_power_of_2 >> LeafTypePackedIndexShift);
        // 全ノードを格納可能なサイズの木の深さ と インデックス計算簡易化のため1ベースインデックスのために +2
        const auto packed_node_depth = ngl::MostSignificantBit32(required_packed_leaf_count) + 1;

        const auto packed_node_count = 1u << packed_node_depth;
        
        packed_leaf_count_ = (1 << (packed_node_depth-1));
        packed_leaf_offset_ = packed_node_count >> 1;

        cbt_node_.resize(packed_node_count);
            
        Clear();
    }
    void ConcurrentBinaryTreeU32::Clear()
    {
        // Leafの範囲のみクリア
        memset(&(cbt_node_[packed_leaf_offset_]), 0, packed_leaf_count_ * sizeof(LeafType));
        
        // 合計値クリア.
        cbt_node_[SumValueLocation] = 0;
    }
    void ConcurrentBinaryTreeU32::SetBit(u32 index, u32 bit)
    {
        const u32 packed_leaf_node_index = (index >> LeafTypePackedIndexShift);
        const u32 packed_leaf_bit_location = (LeafTypeBitLocalIndexMask & index);
        const u32 packed_leaf_node_location = packed_leaf_offset_ + packed_leaf_node_index;
        const u32 bit_pattern = (1 << packed_leaf_bit_location);

        if(0 != bit)
        {
            cbt_node_[packed_leaf_node_location] |= bit_pattern;
        }
        else
        {
            cbt_node_[packed_leaf_node_location] &= ~bit_pattern;
        }
    }
    u32 ConcurrentBinaryTreeU32::GetBit(u32 index) const
    {
        const u32 packed_leaf_node_index = (index >> LeafTypePackedIndexShift);
        const u32 packed_leaf_bit_location = (LeafTypeBitLocalIndexMask & index);
        const u32 packed_leaf_node_location = packed_leaf_offset_ + packed_leaf_node_index;

        return (cbt_node_[packed_leaf_node_location] >> packed_leaf_bit_location) & 0x01;
    }

    u32 ConcurrentBinaryTreeU32::GetSum() const
    {
        return cbt_node_[SumValueLocation];// 1ベースのインデックス付け.
    }
    void ConcurrentBinaryTreeU32::SumReduction()
    {
        {
            // Leafのbitカウント.
            const auto leaf_parent_start = packed_leaf_offset_ >> 1;
            for(u32 i = 0; i < (packed_leaf_count_ >> 1); ++i)
            {
                const auto target_node_location = leaf_parent_start + i;
                const auto bit_count_leaf_2 = ngl::Count32bit(cbt_node_[((target_node_location) << 1) + 0]) + ngl::Count32bit(cbt_node_[(target_node_location << 1) + 1]);
                cbt_node_[target_node_location] = bit_count_leaf_2;
            }
        }

        // bitカウントよりも親の通常バイナリツリー合計.
        for(int d = 2; d <= ngl::MostSignificantBit32(packed_leaf_count_); ++d)
        {
            const auto leaf_parent_start = packed_leaf_offset_ >> d;
            for(u32 i = 0; i < (packed_leaf_count_ >> d); ++i)
            {
                const auto target_node_location = leaf_parent_start + i;
                const auto bit_count_leaf_2 = cbt_node_[((target_node_location) << 1) + 0] + cbt_node_[(target_node_location << 1) + 1];
                cbt_node_[target_node_location] = bit_count_leaf_2;
            }
        }
    }
    // 下位から i番目 の 1 の位置を検索. SumReduction後に使用可能.
    int ConcurrentBinaryTreeU32::Find_ith_Bit1(u32 i)
    {
        assert(false && u8"未実装");
        return -1;
    }
    // 下位から i番目 の 0 の位置を検索. SumReduction後に使用可能.
    int ConcurrentBinaryTreeU32::Find_ith_Bit0(u32 i)
    {
        assert(false && u8"未実装");
        return -1;
    }

    u32 ConcurrentBinaryTreeU32::NumLeaf() const
    {
        return packed_leaf_count_ * LeafTypeBitWidth;
    }

    // テストコード.
    void ConcurrentBinaryTreeU32::Test()
    {
        ConcurrentBinaryTreeU32 cbt;
        cbt.Initialize(513);
        
        cbt.SetBit(0, 1);
        cbt.SetBit(1, 1);
        cbt.SetBit(3, 1);
        cbt.SetBit(513, 1);
        assert(1 == cbt.GetBit(0));
        assert(1 == cbt.GetBit(1));
        assert(0 == cbt.GetBit(2));
        assert(1 == cbt.GetBit(3));
        assert(0 == cbt.GetBit(4));
        assert(1 == cbt.GetBit(513));
        cbt.SumReduction();
        assert(4 == cbt.GetSum());

        /*
        // i番目の1の位置.
        const auto bit1_location_0 = cbt.Find_ith_Bit1(0);
        const auto bit1_location_1 = cbt.Find_ith_Bit1(1);
        const auto bit1_location_2 = cbt.Find_ith_Bit1(2);
        const auto bit1_location_3 = cbt.Find_ith_Bit1(3);
        const auto bit1_location_4 = cbt.Find_ith_Bit1(4);
        assert(0 == bit1_location_0);
        assert(1 == bit1_location_1);
        assert(3 == bit1_location_2);
        assert(513 == bit1_location_3);
        assert(-1 == bit1_location_4);

        // i番目の0の位置.
        const auto bit0_location_0 = cbt.Find_ith_Bit0(0);
        const auto bit0_location_1 = cbt.Find_ith_Bit0(1);
        const auto bit0_location_2 = cbt.Find_ith_Bit0(2);
        const auto bit0_location_3 = cbt.Find_ith_Bit0(3);
        const auto bit0_location_4 = cbt.Find_ith_Bit0(4);
        assert(2 == bit0_location_0);
        assert(4 == bit0_location_1);
        assert(5 == bit0_location_2);
        assert(6 == bit0_location_3);
        assert(7 == bit0_location_4);
        */


        cbt.Clear();

        const auto num_leaf = cbt.NumLeaf();
        for(u32 i = 0; i < cbt.NumLeaf(); ++i)
        {
            cbt.SetBit(i, 1);
        }
        cbt.SumReduction();
        assert(cbt.NumLeaf() == cbt.GetSum());
        
        cbt.Clear();
    }




    SwTessellationMesh::~SwTessellationMesh()
    {
        Finalize();
    }

    // 初期化
    bool SwTessellationMesh::Initialize(
        ngl::rhi::DeviceDep* p_device,
        ngl::fwk::GfxScene* gfx_scene,
        const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh)
    {
        // 専用にマテリアル指定.
        constexpr text::HashText<64> attrles_material_name = "opaque_attrless";

        if (!Super::Initialize(p_device, gfx_scene, res_mesh, attrles_material_name.Get()))
        {
            assert(false);
            return false;
        }

        const auto shape_count  =res_mesh->data_.shape_array_.size();
        
        // HalfEdge生成.
        half_edge_mesh_array_.resize(shape_count);
        for (int i = 0; i < res_mesh->data_.shape_array_.size(); ++i)
        {
            // Shape単位.
            const gfx::MeshShapePart& shape = res_mesh->data_.shape_array_[i];
            half_edge_mesh_array_[i].Initialize(shape.index_.GetTypedRawDataPtr(), shape.num_primitive_ * 3);
        }

        // HalfEdgeのShaderResource作成.
        half_edge_buffer_array_.resize(half_edge_mesh_array_.size());
        half_edge_srv_array_.resize(half_edge_mesh_array_.size());
        for (int i = 0; i < half_edge_mesh_array_.size(); ++i)
        {
            // Shape単位.
            half_edge_buffer_array_[i] = new rhi::BufferDep();
            {
                rhi::BufferDep::Desc desc = {};
                desc.heap_type            = rhi::EResourceHeapType::Upload;
                desc.bind_flag            = rhi::ResourceBindFlag::ShaderResource;
                desc.element_byte_size    = sizeof(HalfEdge);
                desc.element_count        = static_cast<u32>(half_edge_mesh_array_[i].half_edge_.size());
                half_edge_buffer_array_[i]->Initialize(p_device, desc);
            }
            {
                if (auto* mapped_ptr = half_edge_buffer_array_[i]->MapAs<HalfEdge>())
                {
                    memcpy(mapped_ptr, half_edge_mesh_array_[i].half_edge_.data(), sizeof(HalfEdge) * half_edge_mesh_array_[i].half_edge_.size());

                    half_edge_buffer_array_[i]->Unmap();
                }
            }
            half_edge_srv_array_[i] = new rhi::ShaderResourceViewDep();
            {
                if (!half_edge_srv_array_[i]->InitializeAsStructured(p_device, half_edge_buffer_array_[i].Get(), sizeof(HalfEdge), 0, static_cast<u32>(half_edge_mesh_array_[i].half_edge_.size())))
                {
                    assert(false);
                    return false;
                }
            }
        }

        // Game更新のコールバックを設定.
        SetGameUpdateCallback(
            [this](gfx::scene::SceneMeshGameUpdateCallbackArg arg)
            {
                UpdateOnGame(arg);
            });

        // Render更新のコールバックを設定.
        SetRenderUpdateCallback(
            [this](gfx::scene::SceneMeshRenderUpdateCallbackArg arg)
            {
                UpdateOnRender(arg);
            });

        // AttrLess用のBindResourceコールバック.
        SetBindModelResourceOptionCallback(
            [this](gfx::BindModelResourceOptionCallbackArg arg)
            {
                auto* pso       = arg.pso;
                auto* desc_set  = arg.desc_set;
                int shape_index = arg.shape_index;

                auto* shape = &(GetModel()->res_mesh_->data_.shape_array_[shape_index]);

                // 追加でHalfEdgeバッファなどを設定.
                pso->SetView(desc_set, "half_edge_buffer", half_edge_srv_array_[shape_index].Get());
                pso->SetView(desc_set, "vertex_position_buffer", &(shape->position_.rhi_srv));
            });

        // AttrLess用のDrawShape関数オーバーライド.
        SetProceduralDrawShapeFunc(
            [this](gfx::DrawShapeOverrideFuncionArg arg)
            {
                auto* p_command_list = arg.command_list;
                int shape_index      = arg.shape_index;

                // AttributeLess描画のため頂点入力を使わずにDraw発行.
                p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                p_command_list->DrawInstanced(static_cast<u32>(half_edge_mesh_array_[shape_index].half_edge_.size()), 1, 0, 0);
            });

        return true;
    }

    void SwTessellationMesh::UpdateOnGame(gfx::scene::SceneMeshGameUpdateCallbackArg arg)
    {
        // TODO.
    }
    void SwTessellationMesh::UpdateOnRender(gfx::scene::SceneMeshRenderUpdateCallbackArg arg)
    {
        // TODO.
    }

}  // namespace ngl::render::app
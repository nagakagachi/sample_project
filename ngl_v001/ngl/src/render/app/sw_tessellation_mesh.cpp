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

        // HalfEdge生成.
        half_edge_mesh_array_.resize(res_mesh->data_.shape_array_.size());
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
                auto* pso      = arg.pso;
                auto* desc_set = arg.desc_set;
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
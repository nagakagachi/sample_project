/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/sw_tessellation_mesh.h"
#include "gfx/rtg/rtg_common.h"
#include <cmath>
#include <string>


namespace ngl::render::app
{
    // CBTGpuResourcesクラスの実装
    bool CBTGpuResources::Initialize(ngl::rhi::DeviceDep* p_device, uint32_t shape_half_edges, uint32_t average_subdivision_level)
    {
        // パラメータ保存
        total_half_edges = shape_half_edges;
        
        // CBT動的パラメータ計算
        cbt_mesh_minimum_tree_depth = static_cast<uint32_t>(std::ceil(std::log2(std::max(1u, shape_half_edges))));
        cbt_tree_depth = cbt_mesh_minimum_tree_depth + average_subdivision_level;
        
        const uint32_t max_bisectors = 1u << cbt_tree_depth;  // 2^cbt_tree_depth
        this->max_bisectors = max_bisectors;  // メンバー変数に保存
        
        // CBTバッファサイズ計算（新構造）
        // 内部ノード数（インデックス1から2^cbt_tree_depth - 1） + リーフビットフィールド数
        const uint32_t internal_node_count = (1u << cbt_tree_depth) - 1;  // 2^cbt_tree_depth - 1
        const uint32_t leaf_bitfield_count = (max_bisectors + 31) / 32;    // ceil(max_bisectors / 32)
        const uint32_t cbt_total_nodes = internal_node_count + leaf_bitfield_count + 1;  // +1 for index 0 (unused)
        
        // CBT Buffer (uint型の完全二分木) - パフォーマンスのためDefaultヒープを使用
        cbt_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = cbt_total_nodes;
            if (!cbt_buffer->Initialize(p_device, desc)) return false;
        }
        cbt_buffer_srv = new rhi::ShaderResourceViewDep();
        if (!cbt_buffer_srv->InitializeAsStructured(p_device, cbt_buffer.Get(), sizeof(uint32_t), 0, cbt_total_nodes)) return false;
        cbt_buffer_uav = new rhi::UnorderedAccessViewDep();
        if (!cbt_buffer_uav->InitializeAsStructured(p_device, cbt_buffer.Get(), sizeof(uint32_t), 0, cbt_total_nodes)) return false;

        // Bisector Pool Buffer
        bisector_pool_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(Bisector);
            desc.element_count = max_bisectors;
            if (!bisector_pool_buffer->Initialize(p_device, desc)) return false;
        }
        bisector_pool_srv = new rhi::ShaderResourceViewDep();
        if (!bisector_pool_srv->InitializeAsStructured(p_device, bisector_pool_buffer.Get(), sizeof(Bisector), 0, max_bisectors)) return false;
        bisector_pool_uav = new rhi::UnorderedAccessViewDep();
        if (!bisector_pool_uav->InitializeAsStructured(p_device, bisector_pool_buffer.Get(), sizeof(Bisector), 0, max_bisectors)) return false;

        // Index Cache Buffer (int2型)
        index_cache_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(int) * 2;  // int2
            desc.element_count = max_bisectors;
            if (!index_cache_buffer->Initialize(p_device, desc)) return false;
        }
        index_cache_srv = new rhi::ShaderResourceViewDep();
        if (!index_cache_srv->InitializeAsStructured(p_device, index_cache_buffer.Get(), sizeof(int) * 2, 0, max_bisectors)) return false;
        index_cache_uav = new rhi::UnorderedAccessViewDep();
        if (!index_cache_uav->InitializeAsStructured(p_device, index_cache_buffer.Get(), sizeof(int) * 2, 0, max_bisectors)) return false;

        // Alloc Counter Buffer (uint型)
        alloc_counter_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = 1;
            if (!alloc_counter_buffer->Initialize(p_device, desc)) return false;
        }
        alloc_counter_srv = new rhi::ShaderResourceViewDep();
        if (!alloc_counter_srv->InitializeAsStructured(p_device, alloc_counter_buffer.Get(), sizeof(uint32_t), 0, 1)) return false;
        alloc_counter_uav = new rhi::UnorderedAccessViewDep();
        if (!alloc_counter_uav->InitializeAsStructured(p_device, alloc_counter_buffer.Get(), sizeof(uint32_t), 0, 1)) return false;

        // Indirect Dispatch Args Buffers (uint3型)
        indirect_dispatch_arg_for_bisector_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(uint32_t) * 3;  // uint3
            desc.element_count = 1;
            if (!indirect_dispatch_arg_for_bisector_buffer->Initialize(p_device, desc)) return false;
        }
        indirect_dispatch_arg_for_bisector_uav = new rhi::UnorderedAccessViewDep();
        if (!indirect_dispatch_arg_for_bisector_uav->InitializeAsStructured(p_device, indirect_dispatch_arg_for_bisector_buffer.Get(), sizeof(uint32_t) * 3, 0, 1)) return false;

        indirect_dispatch_arg_for_index_cache_buffer = new rhi::BufferDep();
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(uint32_t) * 3;  // uint3
            desc.element_count = 1;
            if (!indirect_dispatch_arg_for_index_cache_buffer->Initialize(p_device, desc)) return false;
        }
        indirect_dispatch_arg_for_index_cache_uav = new rhi::UnorderedAccessViewDep();
        if (!indirect_dispatch_arg_for_index_cache_uav->InitializeAsStructured(p_device, indirect_dispatch_arg_for_index_cache_buffer.Get(), sizeof(uint32_t) * 3, 0, 1)) return false;
        
        return true;
    }

    ngl::rhi::ConstantBufferPooledHandle CBTGpuResources::UpdateConstants(ngl::rhi::DeviceDep* p_device, const ngl::math::Mat34& object_to_world, const ngl::math::Vec3& important_point_world, uint32_t frame_index)
    {
        // ConstantBufferPoolから定数バッファを確保
        auto cbh = p_device->GetConstantBufferPool()->Alloc(sizeof(CBTConstants));
        
        // 定数バッファに全データを書き込み
        if (auto* mapped_ptr = cbh->buffer_.MapAs<CBTConstants>())
        {
            // CBT固有の設定値
            mapped_ptr->cbt_tree_depth = cbt_tree_depth;
            mapped_ptr->cbt_mesh_minimum_tree_depth = cbt_mesh_minimum_tree_depth;
            mapped_ptr->bisector_pool_max_size = max_bisectors;
            mapped_ptr->total_half_edges = total_half_edges;
            
            // フレーム可変データ
            mapped_ptr->frame_index = frame_index;
            
            // 変換行列を更新
            mapped_ptr->object_to_world = object_to_world;
            mapped_ptr->world_to_object = ngl::math::Mat34::Inverse(object_to_world);
            
            // 重要座標を更新
            mapped_ptr->important_point = important_point_world;
            
            // テッセレーション閾値を設定
            mapped_ptr->tessellation_split_threshold = 0.1f;   // 分割閾値
            mapped_ptr->tessellation_merge_factor = 0.5f;      // 統合係数（分割閾値に対する比率, 0.5 = 50%）
            
            cbh->buffer_.Unmap();
        }
        
        return cbh;
    }

    void CBTGpuResources::BindResources(ngl::rhi::ComputePipelineStateDep* pso, ngl::rhi::DescriptorSetDep* desc_set, ngl::rhi::ConstantBufferPooledHandle cb_handle) const
    {
        // Constant Buffer (ConstantBufferPoolから)
        pso->SetView(desc_set, "CBTTessellationConstants", &cb_handle->cbv_);
        
        // CBT Buffer
        pso->SetView(desc_set, "cbt_buffer", cbt_buffer_srv.Get());
        pso->SetView(desc_set, "cbt_buffer_rw", cbt_buffer_uav.Get());
        
        // Bisector Pool
        pso->SetView(desc_set, "bisector_pool", bisector_pool_srv.Get());
        pso->SetView(desc_set, "bisector_pool_rw", bisector_pool_uav.Get());
        
        // Index Cache
        pso->SetView(desc_set, "index_cache", index_cache_srv.Get());
        pso->SetView(desc_set, "index_cache_rw", index_cache_uav.Get());
        
        // Alloc Counter
        pso->SetView(desc_set, "alloc_counter", alloc_counter_srv.Get());
        pso->SetView(desc_set, "alloc_counter_rw", alloc_counter_uav.Get());
        
        // Indirect Dispatch Args
        pso->SetView(desc_set, "indirect_dispatch_arg_for_bisector", indirect_dispatch_arg_for_bisector_uav.Get());
        pso->SetView(desc_set, "indirect_dispatch_arg_for_index_cache", indirect_dispatch_arg_for_index_cache_uav.Get());
    }

    SwTessellationMesh::~SwTessellationMesh()
    {
        Finalize();
    }

    // 初期化
    bool SwTessellationMesh::Initialize(
        ngl::rhi::DeviceDep* p_device,
        ngl::fwk::GfxScene* gfx_scene,
        const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh,
        uint32_t average_subdivision_level)
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

        // CBT Tessellation Compute Shaders初期化
        {
            // Helper function to create compute shader PSO
            auto CreateComputePSO = [&](const char* shader_path) -> ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>
            {
                auto pso = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
                ngl::rhi::ComputePipelineStateDep::Desc cpso_desc = {};
                {
                    ngl::gfx::ResShader::LoadDesc cs_load_desc = {};
                    cs_load_desc.stage = ngl::rhi::EShaderStage::Compute;
                    cs_load_desc.shader_model_version = k_shader_model;
                    cs_load_desc.entry_point_name = "main_cs";
                    auto cs_load_handle = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                        p_device, NGL_RENDER_SHADER_PATH(shader_path), &cs_load_desc
                    );
                    cpso_desc.cs = &cs_load_handle->data_;
                }
                pso->Initialize(p_device, cpso_desc);
                return pso;
            };

            // Initialize all compute shaders
            cbt_init_leaf_pso_ = CreateComputePSO("sw_tess/cbt_tess_init_leaf_cs.hlsl");
            cbt_begin_update_pso_ = CreateComputePSO("sw_tess/cbt_tess_begin_update_cs.hlsl");
            cbt_cache_index_pso_ = CreateComputePSO("sw_tess/cbt_tess_cache_index_cs.hlsl");
            cbt_reset_command_pso_ = CreateComputePSO("sw_tess/cbt_tess_reset_command_cs.hlsl");
            cbt_generate_command_pso_ = CreateComputePSO("sw_tess/cbt_tess_generate_command_cs.hlsl");
            cbt_reserve_block_pso_ = CreateComputePSO("sw_tess/cbt_tess_reserve_block_cs.hlsl");
            cbt_fill_new_block_pso_ = CreateComputePSO("sw_tess/cbt_tess_fill_new_block_cs.hlsl");
            cbt_update_neighbor_pso_ = CreateComputePSO("sw_tess/cbt_tess_update_neighbor_cs.hlsl");
            cbt_update_cbt_bitfield_pso_ = CreateComputePSO("sw_tess/cbt_tess_update_cbt_bitfield_cs.hlsl");
            cbt_sum_reduction_pso_ = CreateComputePSO("sw_tess/cbt_sum_reduction_naive_cs.hlsl");
            cbt_end_update_pso_ = CreateComputePSO("sw_tess/cbt_tess_end_update_cs.hlsl");
        }

        // CBT Tessellation Buffers初期化 (シェイプ単位で管理)
        {
            // 平均分割レベルを保存
            average_subdivision_level_ = average_subdivision_level;
            
            // シェイプ数分のCBTリソース配列を準備
            const auto shape_count = half_edge_mesh_array_.size();
            cbt_gpu_resources_array_.resize(shape_count);
            
            // シェイプ単位でCBTリソースを初期化
            for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
            {
                // 各シェイプのHalfEdge数を取得
                const uint32_t shape_half_edges = static_cast<uint32_t>(half_edge_mesh_array_[shape_idx].half_edge_.size());
                
                // CBTGpuResourcesを初期化
                if (!cbt_gpu_resources_array_[shape_idx].Initialize(p_device, shape_half_edges, average_subdivision_level_))
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
        // Gameスレッドで実行される更新処理.
        arg.dummy;
    }
    void SwTessellationMesh::UpdateOnRender(gfx::scene::SceneMeshRenderUpdateCallbackArg arg)
    {
        // Renderスレッドで実行される更新処理.
        // command_listに対するシェーダディスパッチなどをする.
        auto* command_list = arg.command_list;
        
        // 定数バッファを毎フレーム更新（ConstantBufferPoolから確保）
        const auto shape_count = half_edge_mesh_array_.size();
        std::vector<ngl::rhi::ConstantBufferPooledHandle> cbt_constant_handles;
        cbt_constant_handles.reserve(shape_count);
        
        for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
        {
            // 基底クラスからオブジェクトワールド変換行列を取得
            ngl::math::Mat34 object_to_world = GetTransform();
            
            // TODO: カメラ位置の適切な取得方法を実装
            // Important Point（テッセレーション評価で重視する座標）
            ngl::math::Vec3 important_point_world = important_point_world_;
            
            // TODO: フレーム番号の適切な取得方法を実装
            // 現在は暫定的に固定値を使用
            uint32_t frame_index = 0;
            
            auto cb_handle = cbt_gpu_resources_array_[shape_idx].UpdateConstants(command_list->GetDevice(), object_to_world, important_point_world, frame_index);
            cbt_constant_handles.push_back(cb_handle);
        }

        // CBT Tessellation Pipeline (シェイプ単位で実行)
        // TODO: 現在はBisectorPool総数に対してDispatchやDrawしているシェーダがあるが、
        //       将来的にはBeginUpdateでIndirectArgを生成して最小限のIndirect命令発行に変更する
        {
            const auto shape_count = half_edge_mesh_array_.size();

            // 初回のみCBT初期化を実行
            if (!cbt_initialized_)
            {
                for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
                {
                    // 1. リーフノード初期化
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_InitializeLeaf_Shape" + std::to_string(shape_idx)).c_str());
                        
                        command_list->SetPipelineState(cbt_init_leaf_pso_.Get());
                        
                        ngl::rhi::DescriptorSetDep desc_set = {};
                        cbt_gpu_resources_array_[shape_idx].BindResources(cbt_init_leaf_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                        
                        // HalfEdgeバッファを追加でバインド
                        cbt_init_leaf_pso_->SetView(&desc_set, "half_edge_buffer", half_edge_srv_array_[shape_idx].Get());
                        
                        command_list->SetDescriptorSet(cbt_init_leaf_pso_.Get(), &desc_set);
                        
                        // HalfEdge数をワークサイズとしてDispatchHelper使用
                        uint32_t half_edge_work_size = static_cast<uint32_t>(half_edge_mesh_array_[shape_idx].half_edge_.size());
                        cbt_init_leaf_pso_->DispatchHelper(command_list, half_edge_work_size, 1, 1);
                    }

                    // 2. Sum Reduction実行
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_InitializeSumReduction_Shape" + std::to_string(shape_idx)).c_str());
                        
                        command_list->SetPipelineState(cbt_sum_reduction_pso_.Get());
                        
                        ngl::rhi::DescriptorSetDep desc_set = {};
                        cbt_gpu_resources_array_[shape_idx].BindResources(cbt_sum_reduction_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                        command_list->SetDescriptorSet(cbt_sum_reduction_pso_.Get(), &desc_set);
                        
                        cbt_sum_reduction_pso_->DispatchHelper(command_list, 1, 1, 1);
                    }
                }
                
                cbt_initialized_ = true;
            }
            
            // シェイプ単位でCBT処理を実行
            for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
            {
                // 1. Begin Update Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_BeginUpdate_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_begin_update_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_begin_update_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_begin_update_pso_.Get(), &desc_set);
                    
                    cbt_begin_update_pso_->DispatchHelper(command_list, 1, 1, 1);
                }

                // 2. Cache Index Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_CacheIndex_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_cache_index_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_cache_index_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_cache_index_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_cache_index_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 3. Reset Command Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_ResetCommand_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_reset_command_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_reset_command_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_reset_command_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_reset_command_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 4. Generate Command Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_GenerateCommand_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_generate_command_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_generate_command_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    
                    // Generate Commandパス用の追加リソースバインド
                    auto* shape = &(GetModel()->res_mesh_->data_.shape_array_[shape_idx]);
                    cbt_generate_command_pso_->SetView(&desc_set, "half_edge_buffer", half_edge_srv_array_[shape_idx].Get());
                    cbt_generate_command_pso_->SetView(&desc_set, "vertex_position_buffer", &(shape->position_.rhi_srv));
                    
                    command_list->SetDescriptorSet(cbt_generate_command_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_generate_command_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 5. Reserve Block Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_ReserveBlock_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_reserve_block_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_reserve_block_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_reserve_block_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_reserve_block_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 6. Fill New Block Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_FillNewBlock_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_fill_new_block_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_fill_new_block_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_fill_new_block_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_fill_new_block_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 7. Update Neighbor Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_UpdateNeighbor_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_update_neighbor_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_update_neighbor_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_update_neighbor_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_update_neighbor_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 8. Update CBT Bitfield Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_UpdateCBTBitfield_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_update_cbt_bitfield_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_update_cbt_bitfield_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_update_cbt_bitfield_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_update_cbt_bitfield_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }

                // 9. Sum Reduction Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_SumReduction_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_sum_reduction_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_sum_reduction_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_sum_reduction_pso_.Get(), &desc_set);
                    
                    cbt_sum_reduction_pso_->DispatchHelper(command_list, 1, 1, 1);
                }

                // 10. End Update Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_EndUpdate_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_end_update_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_end_update_pso_.Get(), &desc_set, cbt_constant_handles[shape_idx]);
                    command_list->SetDescriptorSet(cbt_end_update_pso_.Get(), &desc_set);
                    
                    // Bisector総数をワークサイズとしてDispatchHelper使用
                    uint32_t bisector_work_size = cbt_gpu_resources_array_[shape_idx].max_bisectors;
                    cbt_end_update_pso_->DispatchHelper(command_list, bisector_work_size, 1, 1);
                }
            }
        }
    }

}  // namespace ngl::render::app
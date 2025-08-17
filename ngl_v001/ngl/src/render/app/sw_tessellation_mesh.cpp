/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/sw_tessellation_mesh.h"
#include "gfx/rtg/rtg_common.h"
#include <cmath>
#include <string>


namespace ngl::render::app
{

    // RhiBufferSetクラスの実装
    bool RhiBufferSet::InitializeAsStructured(ngl::rhi::DeviceDep* p_device,const rhi::BufferDep::Desc& desc)
    {
        resource_state = desc.initial_state;

        buffer = new rhi::BufferDep();
        if (!buffer->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav = new rhi::UnorderedAccessViewDep();
            if (!uav->InitializeAsStructured(p_device, buffer.Get(), desc.element_byte_size, 0, desc.element_count)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv = new rhi::ShaderResourceViewDep();
            if (!srv->InitializeAsStructured(p_device, buffer.Get(), desc.element_byte_size, 0, desc.element_count)) return false;
        }

        return true;
    }
    bool RhiBufferSet::InitializeAsTyped(ngl::rhi::DeviceDep* p_device, const rhi::BufferDep::Desc& desc, rhi::EResourceFormat format)
    {
        resource_state = desc.initial_state;

        buffer = new rhi::BufferDep();
        if (!buffer->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav = new rhi::UnorderedAccessViewDep();
            if (!uav->InitializeAsTyped(p_device, buffer.Get(), format, 0, desc.element_count)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv = new rhi::ShaderResourceViewDep();
            if (!srv->InitializeAsTyped(p_device, buffer.Get(), format, 0, desc.element_count)) return false;
        }

        return true;
    }

    void RhiBufferSet::ResourceBarrier(ngl::rhi::GraphicsCommandListDep* p_command_list, rhi::EResourceState next_state)
    {
        p_command_list->ResourceBarrier(buffer.Get(), resource_state, next_state);
        resource_state = next_state;// 内部ステート更新.
    }


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
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = cbt_total_nodes;
            if (!cbt_buffer.InitializeAsStructured(p_device, desc)) return false;
        }

        // Bisector Pool Buffer
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(Bisector);
            desc.element_count = max_bisectors;
            if (!bisector_pool_buffer.InitializeAsStructured(p_device, desc)) return false;
        }

        // Index Cache Buffer (int2型)
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(int) * 2;  // int2
            desc.element_count = max_bisectors;
            if (!index_cache_buffer.InitializeAsStructured(p_device, desc)) return false;
        }

        // Alloc Counter Buffer (uint型)
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess;
            desc.element_byte_size = sizeof(int32_t);
            desc.element_count = 1;
            if (!alloc_counter_buffer.InitializeAsStructured(p_device, desc)) return false;
        }

        // Indirect Dispatch Args Buffers (uint3型)
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = 3;
            if (!indirect_dispatch_arg_for_bisector_buffer.InitializeAsTyped(p_device, desc, rhi::EResourceFormat::Format_R32_UINT)) return false;
        }

        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = 3;
            if (!indirect_dispatch_arg_for_index_cache_buffer.InitializeAsTyped(p_device, desc, rhi::EResourceFormat::Format_R32_UINT)) return false;
        }
        
        // Draw Indirect Args Buffer (uint4型: VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation)
        {
            rhi::BufferDep::Desc desc = {};
            desc.heap_type = rhi::EResourceHeapType::Default;
            desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::IndirectArg;
            desc.element_byte_size = sizeof(uint32_t);
            desc.element_count = 4;
            if (!draw_indirect_arg_buffer.InitializeAsTyped(p_device, desc, rhi::EResourceFormat::Format_R32_UINT)) return false;
        }


        return true;
    }

    ngl::rhi::ConstantBufferPooledHandle CBTGpuResources::UpdateConstants(ngl::rhi::DeviceDep* p_device, 
        const ngl::math::Mat34& object_to_world, const ngl::math::Vec3& important_point_world, bool tessellation_update, float tessellation_split_threshold, int32_t fixed_subdivision_level, int32_t debug_bisector_neighbor, int32_t debug_target_bisector_id, int32_t debug_target_bisector_depth)
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
            
            
            // 固定分割レベル
            mapped_ptr->fixed_subdivision_level = fixed_subdivision_level;
            
            // 変換行列を更新
            mapped_ptr->object_to_world = object_to_world;
            mapped_ptr->world_to_object = ngl::math::Mat34::Inverse(object_to_world);
            
            // 重要座標を更新
            mapped_ptr->important_point = important_point_world;
            
            // テッセレーション閾値を設定
            mapped_ptr->tessellation_split_threshold = tessellation_split_threshold;
            mapped_ptr->tessellation_merge_factor = 0.45f;      // 統合係数（分割閾値に対する比率, 0.5 = 50%）
            
            // デバッグその他.
            mapped_ptr->tessellation_debug_flag = debug_bisector_neighbor;

            // テッセレーション更新フラグ
            mapped_ptr->tessellation_update = tessellation_update ? 1 : 0;

            // デバッグ対象Bisector情報
            mapped_ptr->debug_target_bisector_id = debug_target_bisector_id;
            mapped_ptr->debug_target_bisector_depth = debug_target_bisector_depth;

            cbh->buffer_.Unmap();
        }
        
        return cbh;
    }

    void CBTGpuResources::BindResources(ngl::rhi::ComputePipelineStateDep* pso, ngl::rhi::DescriptorSetDep* desc_set, ngl::rhi::ConstantBufferPooledHandle cb_handle) const
    {
        // Constant Buffer (ConstantBufferPoolから)
        pso->SetView(desc_set, "CBTTessellationConstants", &cb_handle->cbv_);
        
        // CBT Buffer
        pso->SetView(desc_set, "cbt_buffer", cbt_buffer.srv.Get());
        pso->SetView(desc_set, "cbt_buffer_rw", cbt_buffer.uav.Get());
        
        // Bisector Pool
        pso->SetView(desc_set, "bisector_pool", bisector_pool_buffer.srv.Get());
        pso->SetView(desc_set, "bisector_pool_rw", bisector_pool_buffer.uav.Get());
        
        // Index Cache
        pso->SetView(desc_set, "index_cache", index_cache_buffer.srv.Get());
        pso->SetView(desc_set, "index_cache_rw", index_cache_buffer.uav.Get());
        
        // Alloc Counter
        pso->SetView(desc_set, "alloc_counter", alloc_counter_buffer.srv.Get());
        pso->SetView(desc_set, "alloc_counter_rw", alloc_counter_buffer.uav.Get());
        
        // Indirect Dispatch Args
        pso->SetView(desc_set, "indirect_dispatch_arg_for_bisector", indirect_dispatch_arg_for_bisector_buffer.uav.Get());
        pso->SetView(desc_set, "indirect_dispatch_arg_for_index_cache", indirect_dispatch_arg_for_index_cache_buffer.uav.Get());

        // Draw Indirect Args
        pso->SetView(desc_set, "draw_indirect_arg", draw_indirect_arg_buffer.uav.Get());
    }

    SwTessellationMesh::~SwTessellationMesh()
    {
        Finalize();
    }

    // 初期化
    bool SwTessellationMesh::Initialize(
        ngl::rhi::DeviceDep* p_device,
        ngl::fwk::GfxScene* gfx_scene,
        const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh, std::shared_ptr<gfx::MeshData> override_mesh_shape_data,
        uint32_t average_subdivision_level)
    {
        // 専用にマテリアル指定.
        constexpr text::HashText<64> attrles_material_name = "opaque_attrless";

        if (!Super::Initialize(p_device, gfx_scene, res_mesh, override_mesh_shape_data, attrles_material_name.Get()))
        {
            assert(false);
            return false;
        }

        {
            // OverrideShapeがある場合はそちらでHalfEdge初期化.
            const auto* shape_array = override_mesh_shape_data ? &override_mesh_shape_data->shape_array_ : &res_mesh->data_.shape_array_;

            const auto shape_count  = shape_array->size();

            // HalfEdge生成.
            half_edge_mesh_array_.resize(shape_count);
            for (int i = 0; i < shape_count; ++i)
            {
                // Shape単位.
                const gfx::MeshShapePart& shape = (*shape_array)[i];
                half_edge_mesh_array_[i].Initialize(shape.index_.GetTypedRawDataPtr(), shape.num_primitive_ * 3);
            }
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
            cbt_sum_reduction_pso_ = CreateComputePSO("sw_tess/cbt_sum_reduction_cs.hlsl");
            cbt_sum_reduction_naive_pso_ = CreateComputePSO("sw_tess/cbt_sum_reduction_naive_cs.hlsl");
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

                if(shape_index >= GetModel()->NumShape())
                {
                    return;
                }
                auto* shape = GetModel()->GetShape(shape_index);

                // 追加でHalfEdgeバッファなどを設定.
                pso->SetView(desc_set, "half_edge_buffer", half_edge_srv_array_[shape_index].Get());
                pso->SetView(desc_set, "vertex_position_buffer", &(shape->position_.rhi_srv));
                
                // CBTテッセレーション用バッファを追加バインド
                pso->SetView(desc_set, "index_cache", cbt_gpu_resources_array_[shape_index].index_cache_buffer.srv.Get());
                pso->SetView(desc_set, "bisector_pool", cbt_gpu_resources_array_[shape_index].bisector_pool_buffer.srv.Get());

                // CBTテッセレーション定数バッファをバインド（最新フレームの定数バッファを使用）
                if (!cbt_constant_handles_.empty() && shape_index < cbt_constant_handles_.size())
                {
                    pso->SetView(desc_set, "CBTTessellationConstants", &cbt_constant_handles_[shape_index]->cbv_);
                }
            });

        // AttrLess用のDrawShape関数オーバーライド.
        SetProceduralDrawShapeFunc(
            [this](gfx::DrawShapeOverrideFuncionArg arg)
            {
                auto* p_command_list = arg.command_list;
                int shape_index      = arg.shape_index;

                // CBTテッセレーション結果をIndirect描画で実行
                p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                
                // 該当するシェイプのdraw_indirect_arg_bufferを使用してIndirect描画
                p_command_list->DrawIndirect(cbt_gpu_resources_array_[shape_index].draw_indirect_arg_buffer.buffer.Get());
            });

        return true;
    }

    void SwTessellationMesh::UpdateOnGame(gfx::scene::SceneMeshGameUpdateCallbackArg arg)
    {
        // Gameスレッドで実行される更新処理.
        arg.dummy;

        ++local_frame_index_;
    }
    void SwTessellationMesh::UpdateOnRender(gfx::scene::SceneMeshRenderUpdateCallbackArg arg)
    {
        // Renderスレッドで実行される更新処理.
        // command_listに対するシェーダディスパッチなどをする.
        auto* command_list = arg.command_list;

        const int cur_local_frame_render_index_ = local_frame_render_index_;
        ++local_frame_render_index_;
        



        // 定数バッファを毎フレーム更新（ConstantBufferPoolから確保）
        const auto shape_count = half_edge_mesh_array_.size();
        cbt_constant_handles_.clear();
        cbt_constant_handles_.reserve(shape_count);
        


        const bool cur_update_enable = tessellation_update_on_render_;
        tessellation_update_on_render_ = tessellation_update_;


        for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
        {
            // 基底クラスからオブジェクトワールド変換行列を取得
            ngl::math::Mat34 object_to_world = GetTransform();
            
            // TODO: カメラ位置の適切な取得方法を実装
            // Important Point（テッセレーション評価で重視する座標）
            ngl::math::Vec3 important_point_world = important_point_world_;

            auto cb_handle = cbt_gpu_resources_array_[shape_idx].UpdateConstants(command_list->GetDevice(), object_to_world, important_point_world, cur_update_enable, tessellation_split_threshold_, fixed_subdivision_level_, debug_bisector_neighbor_, debug_target_bisector_id_, debug_target_bisector_depth_);
            cbt_constant_handles_.push_back(cb_handle);
        }
        
        // 9. Sum Reduction Pass
        auto execute_sum_reduction_cs = [this](rhi::GraphicsCommandListDep* command_list, size_t shape_idx)
        {
            #if 1
                // マルチパスのSumReduction.
                {
                    struct CbCbtSumReduction
                    {
                        // Dispatch側で log2(CBT_SUM_REDUCTION_WIDTH)ずつDepthを減らして呼び出す.
                        u32 target_depth;

                        u32 padding0;
                        u32 padding1;
                        u32 padding2;
                    };

                    // CSはワークバッファをThreadGroup単位で分担してその範囲内でSumReductionを実行するため, ThreadGroupサイズは2の冪となっている. そのMSB==処理Depthサイズ.
                    const int sum_reduction_cs_work_depth_per_pass = MostSignificantBit32(cbt_sum_reduction_pso_->GetThreadGroupSizeX());//10;

                    int sum_reduction_work_size = static_cast<int>(cbt_gpu_resources_array_[shape_idx].max_bisectors);
                    int sum_reduction_cbt_depth = cbt_gpu_resources_array_[shape_idx].cbt_tree_depth;

                    for(; sum_reduction_cbt_depth > 0; sum_reduction_cbt_depth -= sum_reduction_cs_work_depth_per_pass)
                    {
                        // ConstantBufferPoolから定数バッファを確保
                        auto cbh = command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(CbCbtSumReduction));
                        // 定数バッファに全データを書き込み
                        if (auto* mapped_ptr = cbh->buffer_.MapAs<CbCbtSumReduction>())
                        {
                            mapped_ptr->target_depth = sum_reduction_cbt_depth;

                            cbh->buffer_.Unmap();
                        }

                        {
                            NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_SumReduction_shape" + std::to_string(shape_idx) + "_depth" + std::to_string(sum_reduction_cbt_depth)).c_str());

                            command_list->SetPipelineState(cbt_sum_reduction_pso_.Get());
                            
                            ngl::rhi::DescriptorSetDep desc_set = {};
                            {
                                cbt_gpu_resources_array_[shape_idx].BindResources(cbt_sum_reduction_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                                cbt_sum_reduction_pso_->SetView(&desc_set, "cb_sum_reduction", &cbh->cbv_);
                            }
                            command_list->SetDescriptorSet(cbt_sum_reduction_pso_.Get(), &desc_set);

                            cbt_sum_reduction_pso_->DispatchHelper(command_list, sum_reduction_work_size, 1, 1);

                            command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].cbt_buffer.buffer.Get() );
                        }
                        

                        // 次のSumReductionのワークサイズを計算.
                        sum_reduction_work_size = sum_reduction_work_size >> sum_reduction_cs_work_depth_per_pass;
                    }
                }
            #else
                {
                    // 検証用のナイーブ実装SumReduction. シングルスレッド.
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_SumReductionSingleThread_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_sum_reduction_naive_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_sum_reduction_naive_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_sum_reduction_naive_pso_.Get(), &desc_set);

                    cbt_sum_reduction_naive_pso_->DispatchHelper(command_list, 1, 1, 1);

                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].cbt_buffer.buffer.Get() );
                }
            #endif
        };


        // CBT Tessellation Pipeline (シェイプ単位で実行)
        {
            const auto shape_count = half_edge_mesh_array_.size();

            // 初回及びリセットリクエストがある場合.
            if (reset_request_)
            {
                for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
                {
                    // 1. リーフノード初期化
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_InitializeLeaf_Shape" + std::to_string(shape_idx)).c_str());
                        
                        command_list->SetPipelineState(cbt_init_leaf_pso_.Get());
                        
                        ngl::rhi::DescriptorSetDep desc_set = {};
                        cbt_gpu_resources_array_[shape_idx].BindResources(cbt_init_leaf_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                        
                        // HalfEdgeバッファを追加でバインド
                        cbt_init_leaf_pso_->SetView(&desc_set, "half_edge_buffer", half_edge_srv_array_[shape_idx].Get());
                        
                        command_list->SetDescriptorSet(cbt_init_leaf_pso_.Get(), &desc_set);
                        
                        // 未使用ビットのクリアをするために最大数をワークサイズとしてDispatch
                        uint32_t max_bisector_work_size = static_cast<uint32_t>(cbt_gpu_resources_array_[shape_idx].max_bisectors);
                        cbt_init_leaf_pso_->DispatchHelper(command_list, max_bisector_work_size, 1, 1);
                        
                        // バリア.
                        command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].cbt_buffer.buffer.Get() );
                    }

                    // SumReduction.
                    execute_sum_reduction_cs(command_list, shape_idx);
                }
                
                reset_request_ = false;
            }
            
            // シェイプ単位でCBT処理を実行
            for (size_t shape_idx = 0; shape_idx < shape_count; ++shape_idx)
            {
                // 1. Begin Update Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_BeginUpdate_Shape" + std::to_string(shape_idx)).c_str());
                    
                    // IndirectArgをUAV書き込みするためのバリア.
                    // MEMO:Dispatch後とペアのこのバリアがない場合にはシェーダ側ロジックに問題がなくてもテッセレーションが破綻しT-Junctionが起きるなどしていた.
                    cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_index_cache_buffer.ResourceBarrier(command_list, rhi::EResourceState::UnorderedAccess);
                    cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.ResourceBarrier(command_list, rhi::EResourceState::UnorderedAccess);
                    cbt_gpu_resources_array_[shape_idx].draw_indirect_arg_buffer.ResourceBarrier(command_list, rhi::EResourceState::UnorderedAccess);


                    command_list->SetPipelineState(cbt_begin_update_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_begin_update_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_begin_update_pso_.Get(), &desc_set);
                    
                    cbt_begin_update_pso_->DispatchHelper(command_list, 1, 1, 1);


                    cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_index_cache_buffer.ResourceBarrier(command_list, rhi::EResourceState::IndirectArgument);
                    cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.ResourceBarrier(command_list, rhi::EResourceState::IndirectArgument);
                    cbt_gpu_resources_array_[shape_idx].draw_indirect_arg_buffer.ResourceBarrier(command_list, rhi::EResourceState::IndirectArgument);
                }

                // 2. Cache Index Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_CacheIndex_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_cache_index_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_cache_index_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_cache_index_pso_.Get(), &desc_set);
                    
                    // 有効なBisectorに対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_index_cache_buffer.buffer.Get());

                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].index_cache_buffer.buffer.Get() );
                }
                
                // 3. Reset Command Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_ResetCommand_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_reset_command_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_reset_command_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_reset_command_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());
                    
                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].bisector_pool_buffer.buffer.Get() );
                }

                // 4. Generate Command Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_GenerateCommand_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_generate_command_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_generate_command_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    
                    // Generate Commandパス用の追加リソースバインド
                    auto* shape = GetModel()->GetShape((int)shape_idx);
                    cbt_generate_command_pso_->SetView(&desc_set, "half_edge_buffer", half_edge_srv_array_[shape_idx].Get());
                    cbt_generate_command_pso_->SetView(&desc_set, "vertex_position_buffer", &(shape->position_.rhi_srv));
                    
                    command_list->SetDescriptorSet(cbt_generate_command_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());

                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].bisector_pool_buffer.buffer.Get() );
                }

                // 5. Reserve Block Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_ReserveBlock_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_reserve_block_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_reserve_block_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_reserve_block_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());
                    
                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].bisector_pool_buffer.buffer.Get() );
                }

                // 6. Fill New Block Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_FillNewBlock_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_fill_new_block_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_fill_new_block_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_fill_new_block_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());

                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].bisector_pool_buffer.buffer.Get() );
                }

                // 7. Update Neighbor Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_UpdateNeighbor_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_update_neighbor_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_update_neighbor_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_update_neighbor_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());

                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].bisector_pool_buffer.buffer.Get() );
                }

                // 8. Update CBT Bitfield Pass
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, ("CBT_UpdateCBTBitfield_Shape" + std::to_string(shape_idx)).c_str());
                    
                    command_list->SetPipelineState(cbt_update_cbt_bitfield_pso_.Get());
                    
                    ngl::rhi::DescriptorSetDep desc_set = {};
                    cbt_gpu_resources_array_[shape_idx].BindResources(cbt_update_cbt_bitfield_pso_.Get(), &desc_set, cbt_constant_handles_[shape_idx]);
                    command_list->SetDescriptorSet(cbt_update_cbt_bitfield_pso_.Get(), &desc_set);
                    
                    // Bisector総数または未割り当てBisectorの最大数に対するDispatchIndirect実行
                    command_list->DispatchIndirect(cbt_gpu_resources_array_[shape_idx].indirect_dispatch_arg_for_bisector_buffer.buffer.Get());

                    // バリア.
                    command_list->ResourceUavBarrier( cbt_gpu_resources_array_[shape_idx].cbt_buffer.buffer.Get() );
                }

                // 9. Sum Reduction Pass
                execute_sum_reduction_cs(command_list, shape_idx);
            }
        }
    }

}  // namespace ngl::render::app
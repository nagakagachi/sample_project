/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "render/app/half_edge_mesh.h"
#include "render/app/concurrent_binary_tree.h"
#include "render/scene/scene_mesh.h"

namespace ngl::render::app
{
    // CBT関連のGPUリソースをまとめたクラス
    class CBTGpuResources
    {
    public:
        // CBT Buffer
        rhi::RefBufferDep cbt_buffer;
        rhi::RefUavDep cbt_buffer_uav;
        rhi::RefSrvDep cbt_buffer_srv;
        
        // Bisector Pool Buffer
        rhi::RefBufferDep bisector_pool_buffer;
        rhi::RefUavDep bisector_pool_uav;
        rhi::RefSrvDep bisector_pool_srv;
        
        // Index Cache Buffer
        rhi::RefBufferDep index_cache_buffer;
        rhi::RefUavDep index_cache_uav;
        rhi::RefSrvDep index_cache_srv;
        
        // Alloc Counter Buffer
        rhi::RefBufferDep alloc_counter_buffer;
        rhi::RefUavDep alloc_counter_uav;
        rhi::RefSrvDep alloc_counter_srv;
        
        // Indirect Dispatch Args Buffers
        rhi::RefBufferDep indirect_dispatch_arg_for_bisector_buffer;
        rhi::RefUavDep indirect_dispatch_arg_for_bisector_uav;
        
        rhi::RefBufferDep indirect_dispatch_arg_for_index_cache_buffer;
        rhi::RefUavDep indirect_dispatch_arg_for_index_cache_uav;

        // CBT Constants
        struct CBTConstants
        {
            uint32_t cbt_tree_depth;
            uint32_t cbt_mesh_minimum_tree_depth;
            uint32_t bisector_pool_max_size;
            uint32_t frame_index;
            uint32_t total_half_edges;  // 初期化用
            uint32_t padding[3];       // 16byte alignment
        };
        rhi::RefBufferDep cbt_constants_buffer;
        rhi::RefCbvDep cbt_constants_cbv;
        
        // CBT initialization data
        uint32_t total_half_edges;
        uint32_t cbt_mesh_minimum_tree_depth;  // log2(HalfEdge数)
        uint32_t cbt_tree_depth;              // CBT最大深さ
        uint32_t max_bisectors;               // Bisector総数 (2^cbt_tree_depth)

        // 初期化メソッド
        bool Initialize(ngl::rhi::DeviceDep* p_device, uint32_t shape_half_edges, uint32_t average_subdivision_level);
        
        // リソースバインド用ヘルパー
        void BindResources(ngl::rhi::ComputePipelineStateDep* pso, ngl::rhi::DescriptorSetDep* desc_set) const;
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
            const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh,
            uint32_t average_subdivision_level = 3);  // 平均分割レベル

    private:
        // Game更新.
        void UpdateOnGame(gfx::scene::SceneMeshGameUpdateCallbackArg arg);
        // Render更新.
        void UpdateOnRender(gfx::scene::SceneMeshRenderUpdateCallbackArg arg);

    private:
        std::vector<HalfEdgeMesh> half_edge_mesh_array_;

        std::vector<rhi::RefBufferDep> half_edge_buffer_array_;
        std::vector<rhi::RefSrvDep> half_edge_srv_array_;

        // CBT Tessellation Compute Shaders
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_init_leaf_pso_ = {};  // リーフ初期化専用
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_begin_update_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_cache_index_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_reset_command_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_generate_command_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_reserve_block_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_fill_new_block_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_update_neighbor_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_update_cbt_bitfield_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_sum_reduction_pso_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> cbt_end_update_pso_ = {};

        // CBT GPU Resources (シェイプ単位で管理)
        std::vector<CBTGpuResources> cbt_gpu_resources_array_;
        
        // CBT共通パラメータ
        uint32_t average_subdivision_level_;
        bool cbt_initialized_ = false;
    };

}  // namespace ngl::render::app

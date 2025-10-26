/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/constant_buffer_pool.h"
#include "render/app/common/render_app_common.h"

namespace ngl::render::app
{

    struct ToroidalGridArea
    {
        math::Vec3i center_cell_id_ = {};
        math::Vec3i center_cell_id_prev_ = {};
        math::Vec3 min_pos_ = {};
        math::Vec3 min_pos_prev_ = {};
        math::Vec3i toroidal_offset_ = {};
        math::Vec3i toroidal_offset_prev_ = {};
        math::Vec3i min_pos_delta_cell_ = {};

        math::Vec3u resolution_ = math::Vec3u(32);
        float       cell_size_ = 3.0f;

        u32 total_count = {};
        
        u32         flatten_2d_width_ = {};
    };
    class ToroidalGridUpdater
    {
    public:
        ToroidalGridUpdater() = default;
        ~ToroidalGridUpdater() = default;

        void Initialize(const math::Vec3u& grid_resolution, float bbv_cell_size);

        void UpdateGrid(const math::Vec3& important_pos);

        const ToroidalGridArea& Get() const { return grid_; }

        math::Vec3i CalcToroidalGridCoordFromLinearCoord(const math::Vec3i& linear_coord) const;
        math::Vec3i CalcLinearGridCoordFromToroidalCoord(const math::Vec3i& toroidal_coord) const;

    private:
        ToroidalGridArea grid_;
    };

    // BitmaskBrickVoxel:Bbv.
    class BitmaskBrickVoxel
    {
    public:
        BitmaskBrickVoxel() = default;
        ~BitmaskBrickVoxel();

        // 初期化
        struct InitArg
        {
            math::Vec3u voxel_resolution = math::Vec3u(32);
            float       voxel_size = 3.0f;
            
            math::Vec3u probe_resolution = math::Vec3u(32);
            float       probe_cell_size = 3.0f;
        };
        bool Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg);

        void Dispatch(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);


        ngl::rhi::ConstantBufferPooledHandle GetDispatchCbh() const { return cbh_dispatch_; }
        rhi::RefSrvDep GetWcpProbeAtlasTex() const { return wcp_probe_atlas_tex_.srv; }

    private:
        bool is_first_dispatch_ = true;

        u32 frame_count_{};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_voxelize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_generate_visible_voxel_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_option_data_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_visible_probe_sampling_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_visible_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_coarse_probe_sampling_and_update_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_coarse_ray_sample_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_fill_probe_octmap_atlas_border_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_debug_visualize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_bbv_debug_probe_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_wcp_debug_probe_ = {};

        ngl::rhi::ConstantBufferPooledHandle cbh_dispatch_ = {};

        // Bitmask Brick Voxel. Bbv.
        // ----------------------------------------------------------------
        ToroidalGridUpdater bbv_grid_updater_ = {};

        ComputeBufferSet bbv_buffer_ = {};
        ComputeBufferSet bbv_optional_data_buffer_ = {};

        // 可視Voxelのみ更新用.
        ngl::u32     bbv_fine_update_voxel_count_max_ = {};
        ComputeBufferSet bbv_fine_update_voxel_list_ = {};
        ComputeBufferSet bbv_fine_update_voxel_indirect_arg_ = {};
        ComputeBufferSet bbv_fine_update_voxel_probe_buffer_ = {};

        // World Cache Probe. Wcp.
        // ----------------------------------------------------------------
        ToroidalGridUpdater wcp_grid_updater_ = {};

        ComputeBufferSet wcp_buffer_ = {};
        ComputeTextureSet wcp_probe_atlas_tex_ = {};

    };

    
    class SsVg
    {
    public:
        static bool dbg_view_enable_;
        static int dbg_view_mode_;
        
        
        static int dbg_bbv_probe_debug_mode_;
        static int dbg_wcp_probe_debug_mode_;
        static float dbg_probe_scale_;
        static float dbg_probe_near_geom_scale_;

    public:
        SsVg() = default;
        ~SsVg();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u wcp_resolution, float wcp_cell_size);
        bool IsValid() const { return is_initialized_; }
        // 破棄
        void Finalize();

        void Dispatch(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

        void SetDescriptorCascade0(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const;

    private:
            bool is_initialized_ = false;
            BitmaskBrickVoxel* ssvg_instance_;
    };

}  // namespace ngl::render::app

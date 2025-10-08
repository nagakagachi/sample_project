/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/constant_buffer_pool.h"
#include "render/app/common/render_app_common.h"

namespace ngl::render::app
{
    class SsVgCascade
    {
    public:
        SsVgCascade() = default;
        ~SsVgCascade();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u base_resolution, float cell_size);

        void Dispatch(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

    private:
        bool is_first_dispatch_ = true;

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_clear_voxel_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_voxelize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_coarse_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_visible_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_visible_probe_post_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_generate_visible_voxel_indirect_arg_ = {};

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_coarse_voxel_update_old_ = {};// 旧バージョン検証.
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_debug_visualize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_debug_obm_voxel_ = {};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};

        u32 frame_count_{};

        
        math::Vec3i grid_center_cell_id_ = {};
        math::Vec3i grid_center_cell_id_prev_ = {};

        math::Vec3 grid_min_pos_ = {};
        math::Vec3 grid_min_pos_prev_ = {};

        math::Vec3i grid_toroidal_offset_ = {};
        math::Vec3i grid_toroidal_offset_prev_ = {};


        math::Vec3u base_resolution_ = math::Vec3u(32);
        float   cell_size_ = 3.0f;
        u32     probe_atlas_texture_base_width_ = {};

        ngl::u32     update_probe_work_count_ = {};

        ComputeBufferSet coarse_voxel_data_ = {};
        ComputeBufferSet occupancy_bitmask_voxel_ = {};
        ComputeTextureSet probe_skyvisibility_ = {};
        
        ComputeBufferSet visible_voxel_list_ = {};
        ComputeBufferSet visible_voxel_indirect_arg_ = {};
        
        ComputeBufferSet visible_voxel_update_probe_ = {};

        ngl::rhi::ConstantBufferPooledHandle cbh_dispatch_ = {};
    };

    
    class SsVg
    {
    public:
        static bool dbg_view_enable_;
        static int dbg_view_mode_;
        static int dbg_probe_debug_view_mode_;
        static int dbg_raytrace_version_;
        static float dbg_probe_scale_;
        static float dbg_probe_near_geom_scale_;

    public:
        SsVg() = default;
        ~SsVg();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u base_resolution, float cell_size, int cascade_count);

        void Dispatch(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

    private:
            std::vector<SsVgCascade*> cascades_;
    };

}  // namespace ngl::render::app

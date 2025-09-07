/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"

#include "render/app/common/render_app_common.h"

namespace ngl::render::app
{
    class SsVg
    {
    public:
        SsVg() = default;
        ~SsVg();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device);

        void Dispatch(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

    private:
        bool is_first_dispatch_ = true;

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_clear_voxel_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_voxelize_ = {};

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_debug_visualize_ = {};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};


        math::Vec3 grid_min_pos_ = {};
        math::Vec3 grid_min_pos_prev_ = {};

        math::Vec3i grid_toroidal_offset_ = {};
        math::Vec3i grid_toroidal_offset_prev_ = {};

        math::Vec3u base_resolution_ = math::Vec3u(128, 64, 128);
        float   cell_size_ = 1.5f;

        RhiBufferSet work_buffer_ = {};
    };

}  // namespace ngl::render::app

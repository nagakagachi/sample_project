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
            rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

    private:

    private:
        // CBT Tessellation Compute Shaders
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ = {};
    };

}  // namespace ngl::render::app

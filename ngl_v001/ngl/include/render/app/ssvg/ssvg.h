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

    private:

    private:
        // CBT Tessellation Compute Shaders
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ = {};
    };

}  // namespace ngl::render::app

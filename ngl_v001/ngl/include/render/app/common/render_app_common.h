/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

namespace ngl::rhi
{
    class DeviceDep;
    class GraphicsCommandListDep;
}  // namespace ngl::rhi

namespace ngl::render::app
{
    class RhiBufferSet
    {
    public:
        RhiBufferSet() = default;
        ~RhiBufferSet() = default;

        bool InitializeAsStructured(ngl::rhi::DeviceDep* p_device, const rhi::BufferDep::Desc& desc);
        bool InitializeAsTyped(ngl::rhi::DeviceDep* p_device, const rhi::BufferDep::Desc& desc, rhi::EResourceFormat format);

        void ResourceBarrier(ngl::rhi::GraphicsCommandListDep* p_command_list, rhi::EResourceState next_state);

    public:
        // CBT Buffer
        rhi::RefBufferDep buffer;
        rhi::RefUavDep uav;
        rhi::RefSrvDep srv;

        rhi::EResourceState resource_state = rhi::EResourceState::Common;
    };

}  // namespace ngl::render::app

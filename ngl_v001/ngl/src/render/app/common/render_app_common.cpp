/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/common/render_app_common.h"

#include "rhi/d3d12/command_list.d3d12.h"

#include <cmath>
#include <string>


namespace ngl::render::app
{

    // RhiBufferSetクラスの実装
    bool RhiBufferSet::InitializeAsStructured(ngl::rhi::DeviceDep* p_device,const rhi::BufferDep::Desc& desc)
    {
        resource_state = desc.initial_state;

        buffer.Reset(new rhi::BufferDep());
        if (!buffer->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav.Reset(new rhi::UnorderedAccessViewDep());
            if (!uav->InitializeAsStructured(p_device, buffer.Get(), desc.element_byte_size, 0, desc.element_count)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv.Reset(new rhi::ShaderResourceViewDep());
            if (!srv->InitializeAsStructured(p_device, buffer.Get(), desc.element_byte_size, 0, desc.element_count)) return false;
        }

        return true;
    }
    bool RhiBufferSet::InitializeAsTyped(ngl::rhi::DeviceDep* p_device, const rhi::BufferDep::Desc& desc, rhi::EResourceFormat format)
    {
        resource_state = desc.initial_state;

        buffer.Reset(new rhi::BufferDep());
        if (!buffer->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav.Reset(new rhi::UnorderedAccessViewDep());
            if (!uav->InitializeAsTyped(p_device, buffer.Get(), format, 0, desc.element_count)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv.Reset(new rhi::ShaderResourceViewDep());
            if (!srv->InitializeAsTyped(p_device, buffer.Get(), format, 0, desc.element_count)) return false;
        }

        return true;
    }

    void RhiBufferSet::ResourceBarrier(ngl::rhi::GraphicsCommandListDep* p_command_list, rhi::EResourceState next_state)
    {
        p_command_list->ResourceBarrier(buffer.Get(), resource_state, next_state);
        resource_state = next_state;// 内部ステート更新.
    }
}  // namespace ngl::render::app
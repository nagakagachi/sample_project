/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/common/render_app_common.h"

#include "rhi/d3d12/command_list.d3d12.h"

#include <cmath>
#include <string>


namespace ngl::render::app
{

    bool ComputeBufferSet::InitializeAsStructured(ngl::rhi::DeviceDep* p_device,const rhi::BufferDep::Desc& desc)
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
    bool ComputeBufferSet::InitializeAsTyped(ngl::rhi::DeviceDep* p_device, const rhi::BufferDep::Desc& desc, rhi::EResourceFormat view_format)
    {
        resource_state = desc.initial_state;

        buffer.Reset(new rhi::BufferDep());
        if (!buffer->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav.Reset(new rhi::UnorderedAccessViewDep());
            if (!uav->InitializeAsTyped(p_device, buffer.Get(), view_format, 0, desc.element_count)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv.Reset(new rhi::ShaderResourceViewDep());
            if (!srv->InitializeAsTyped(p_device, buffer.Get(), view_format, 0, desc.element_count)) return false;
        }

        return true;
    }

    void ComputeBufferSet::ResourceBarrier(ngl::rhi::GraphicsCommandListDep* p_command_list, rhi::EResourceState next_state)
    {
        p_command_list->ResourceBarrier(buffer.Get(), resource_state, next_state);
        resource_state = next_state;// 内部ステート更新.
    }




    bool ComputeTextureSet::Initialize(ngl::rhi::DeviceDep* p_device, const rhi::TextureDep::Desc& desc)
    {
        resource_state = desc.initial_state;

        texture.Reset(new rhi::TextureDep());
        if (!texture->Initialize(p_device, desc)) return false;

        if (desc.bind_flag & rhi::ResourceBindFlag::UnorderedAccess)
        {
            uav.Reset(new rhi::UnorderedAccessViewDep());
            if (!uav->InitializeRwTexture(p_device, texture.Get(), 0, 0, desc.array_size)) return false;
        }
        if (desc.bind_flag & rhi::ResourceBindFlag::ShaderResource)
        {
            srv.Reset(new rhi::ShaderResourceViewDep());
            if (!srv->InitializeAsTexture(p_device, texture.Get(), 0, desc.mip_count, 0, desc.array_size)) return false;
        }

        return true;
    }

    void ComputeTextureSet::ResourceBarrier(ngl::rhi::GraphicsCommandListDep* p_command_list, rhi::EResourceState next_state)
    {
        p_command_list->ResourceBarrier(texture.Get(), resource_state, next_state);
        resource_state = next_state;// 内部ステート更新.
    }
}  // namespace ngl::render::app
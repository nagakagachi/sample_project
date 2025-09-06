/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/ssvg/ssvg.h"

#include "resource/resource_manager.h"
#include "gfx/rtg/rtg_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"
#include "gfx/rendering/global_render_resource.h"

#include <cmath>
#include <string>


namespace ngl::render::app
{

    SsVg::~SsVg()
    {
        
    }

    // 初期化
    bool SsVg::Initialize(ngl::rhi::DeviceDep* p_device)
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
        
        {
            // Initialize all compute shaders
            pso_ = CreateComputePSO("ssvg/ss_voxelize_cs.hlsl");
        }

        return true;
    }

    void SsVg::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
        rhi::ConstantBufferPooledHandle scene_cbv, 
        rhi::RefSrvDep hw_depth_srv,
        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "TaskAfterGBufferInjection");
        
        auto& global_res = gfx::GlobalRenderResource::Instance();

        struct DispatchParam
        {
            ngl::math::Vec2i TexHardwareDepthSize;
        };
        auto cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(DispatchParam));
        {
            auto* p = cbh->buffer_.MapAs<DispatchParam>();

            p->TexHardwareDepthSize = ngl::math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            cbh->buffer_.Unmap();
        }

        ngl::rhi::DescriptorSetDep desc_set = {};
        pso_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
        pso_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);

        pso_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);

        pso_->SetView(&desc_set, "RWTexWork", work_uav.Get());

        p_command_list->SetPipelineState(pso_.Get());
        p_command_list->SetDescriptorSet(pso_.Get(), &desc_set);

        pso_->DispatchHelper(p_command_list, work_tex->GetWidth(), work_tex->GetHeight(), 1);
    }


}  // namespace ngl::render::app
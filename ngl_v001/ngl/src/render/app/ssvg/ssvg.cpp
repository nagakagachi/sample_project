/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/ssvg/ssvg.h"

#include "resource/resource_manager.h"
#include "gfx/rtg/rtg_common.h"

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
}  // namespace ngl::render::app
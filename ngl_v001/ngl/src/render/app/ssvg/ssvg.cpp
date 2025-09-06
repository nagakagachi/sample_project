/*
    sw_tessellation_mesh.cpp
*/

#include "render/app/ssvg/ssvg.h"

#include <cmath>
#include <string>

#include "gfx/command_helper.h"
#include "gfx/rendering/global_render_resource.h"
#include "gfx/rtg/graph_builder.h"
#include "gfx/rtg/rtg_common.h"
#include "resource/resource_manager.h"

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
            auto pso                                          = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
            ngl::rhi::ComputePipelineStateDep::Desc cpso_desc = {};
            {
                ngl::gfx::ResShader::LoadDesc cs_load_desc = {};
                cs_load_desc.stage                         = ngl::rhi::EShaderStage::Compute;
                cs_load_desc.shader_model_version          = k_shader_model;
                cs_load_desc.entry_point_name              = "main_cs";
                auto cs_load_handle                        = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
                    p_device, NGL_RENDER_SHADER_PATH(shader_path), &cs_load_desc);
                cpso_desc.cs = &cs_load_handle->data_;
            }
            pso->Initialize(p_device, cpso_desc);
            return pso;
        };
        {
            // Initialize all compute shaders
            pso_depth_read_ = CreateComputePSO("ssvg/ss_voxelize_cs.hlsl");

            pso_debug_visualize_ = CreateComputePSO("ssvg/ss_voxel_debug_visualize_cs.hlsl");
        }

        {
            work_buffer_.InitializeAsTyped(p_device,
                                           rhi::BufferDep::Desc{
                                               .element_byte_size = 4,
                                               .element_count     = base_resolution_.x * base_resolution_.y * base_resolution_.z,

                                               .bind_flag = rhi::ResourceBindFlag::ShaderResource | rhi::ResourceBindFlag::UnorderedAccess,
                                               .heap_type = rhi::EResourceHeapType::Default},
                                           rhi::EResourceFormat::Format_R32_UINT);
        }

        return true;
    }

    void SsVg::Dispatch(rhi::GraphicsCommandListDep* p_command_list,
                        rhi::ConstantBufferPooledHandle scene_cbv,
                        rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
                        rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav)
    {
        NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "SsVg");

        auto& global_res = gfx::GlobalRenderResource::Instance();

        const math::Vec2i hw_depth_size = math::Vec2i(static_cast<int>(hw_depth_tex->GetWidth()), static_cast<int>(hw_depth_tex->GetHeight()));

        struct DispatchParam
        {
            math::Vec3i BaseResolution;
            u32 Flag;

            math::Vec3 OriginPos;
            float CellSize;
            math::Vec3 MinPos;
            float CellSizeInv;

            math::Vec2i TexHardwareDepthSize;
        };
        auto cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(DispatchParam));
        {
            auto* p = cbh->buffer_.MapAs<DispatchParam>();

            p->BaseResolution = base_resolution_.Cast<int>();
            p->Flag           = 0;

            p->OriginPos = math::Vec3(0.0f, 0.0f, 0.0f);
            p->CellSize  = 1.0f;
            p->MinPos    = p->OriginPos - math::Vec3(static_cast<float>(base_resolution_.x),
                                                     static_cast<float>(base_resolution_.y),
                                                     static_cast<float>(base_resolution_.z)) *
                                           0.5f * p->CellSize;
            p->CellSizeInv = 1.0f / p->CellSize;

            p->TexHardwareDepthSize = hw_depth_size;

            cbh->buffer_.Unmap();
        }

        {
            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_depth_read_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_depth_read_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);

            pso_depth_read_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);

            pso_depth_read_->SetView(&desc_set, "RWBufferWork", work_buffer_.uav.Get());

            p_command_list->SetPipelineState(pso_depth_read_.Get());
            p_command_list->SetDescriptorSet(pso_depth_read_.Get(), &desc_set);

            pso_depth_read_->DispatchHelper(p_command_list, hw_depth_size.x, hw_depth_size.y, 1);


            p_command_list->ResourceUavBarrier(work_buffer_.buffer.Get());
        }
        {
            const math::Vec2i work_tex_size = math::Vec2i(static_cast<int>(work_tex->GetWidth()), static_cast<int>(work_tex->GetHeight()));

            ngl::rhi::DescriptorSetDep desc_set = {};
            pso_debug_visualize_->SetView(&desc_set, "TexHardwareDepth", hw_depth_srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "ngl_cb_sceneview", &scene_cbv->cbv_);

            pso_debug_visualize_->SetView(&desc_set, "cb_dispatch_param", &cbh->cbv_);

            pso_debug_visualize_->SetView(&desc_set, "BufferWork", work_buffer_.srv.Get());
            pso_debug_visualize_->SetView(&desc_set, "RWTexWork", work_uav.Get());

            p_command_list->SetPipelineState(pso_debug_visualize_.Get());
            p_command_list->SetDescriptorSet(pso_debug_visualize_.Get(), &desc_set);

            pso_debug_visualize_->DispatchHelper(p_command_list, work_tex_size.x, work_tex_size.y, 1);
        }
    }

}  // namespace ngl::render::app
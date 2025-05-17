#pragma once

#include "gfx/render/global_render_resource.h"
#include "render/task/pass_common.h"
#include "resource/resource_manager.h"

#include "framework/gfx_render_command_manager.h"

namespace ngl::gfx::scene
{
    class SkyBox
    {
    public:
        SkyBox()
        {
        }
        ~SkyBox() = default;

        bool InitializeAsPanorama(rhi::DeviceDep* p_device, const char* sky_testure_file_path)
        {
            auto& res_mgr = res::ResourceManager::Instance();
            
            gfx::ResTexture::LoadDesc desc{};
            {
                desc.mode = gfx::ResTexture::ECreateMode::FROM_FILE;
            }
            // ソースのパノラマイメージロード.
            res_sky_texture_ = res_mgr.LoadResource<gfx::ResTexture>(p_device, sky_testure_file_path, &desc);


            // 内部で生成するCubemap.
            generated_cubemap_ = new rhi::TextureDep();
            {
                constexpr u32 tex_width = 512;
                rhi::TextureDep::Desc cubemap_desc{};
                rhi::TextureDep::Desc::InitializeAsCubemap(cubemap_desc, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT, tex_width, tex_width, true, true);
                if (!generated_cubemap_->Initialize(p_device, cubemap_desc))
                    assert(false);
            }
            
            generated_cubemap_as_array_uav_ = new rhi::UnorderedAccessViewDep();
            if (!generated_cubemap_as_array_uav_->InitializeRwTexture(p_device, generated_cubemap_.Get(), 0, 0, 6))
                assert(false);
            
            generated_cubemap_srv_ = new rhi::ShaderResourceViewDep();
            if (!generated_cubemap_srv_->InitializeAsTexture(p_device, generated_cubemap_.Get(), 0, 1, 0, 1))
                assert(false);
            
            // PSOセットアップ.
            pso_ = new rhi::ComputePipelineStateDep();
            {
                gfx::ResShader::LoadDesc loaddesc{};
                {
                    loaddesc.entry_point_name = "main";
                    loaddesc.stage = ngl::rhi::EShaderStage::Compute;
                    loaddesc.shader_model_version = "6_3";
                }
                auto res_shader = res_mgr.LoadResource<gfx::ResShader>(p_device,
                                            NGL_RENDER_SHADER_PATH("util/panorama_to_cubemap_cs.hlsl"),
                                            &loaddesc);
                
                rhi::ComputePipelineStateDep::Desc pso_desc{};
                {
                    pso_desc.cs = &res_shader->data_;
                }
                if (!pso_->Initialize(p_device, pso_desc))
                    assert(false);
            }

            // Cubemap生成のRenderCommand登録.
            ngl::fwk::PushCommonRenderCommand([this](ngl::fwk::ComonRenderCommandArg arg)
            {
                auto* command_list = arg.command_list;
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "Generate_Sky_Cubemap_From_Panorama")
                auto& global_res = gfx::GlobalRenderResource::Instance();

                rhi::DescriptorSetDep descset{};
                pso_->SetView(&descset, "tex_panorama", res_sky_texture_->ref_view_.Get());
                pso_->SetView(&descset, "samp", global_res.default_resource_.sampler_linear_clamp.Get());
                pso_->SetView(&descset, "uav_cubemap_as_array", generated_cubemap_as_array_uav_.Get());
                        
                command_list->SetPipelineState(pso_.Get());
                command_list->SetDescriptorSet(pso_.Get(), &descset);
                constexpr u32 k_cubemap_plane_count = 6;
                pso_->DispatchHelper(command_list, generated_cubemap_->GetWidth(), generated_cubemap_->GetHeight(), k_cubemap_plane_count);
            });
            
            return res_sky_texture_.IsValid();
        }

        
        rhi::RefSrvDep GetCubemap() const
        {
            return generated_cubemap_srv_;
        }
        // パノラマ 基本的には確認用.
        res::ResourceHandle<gfx::ResTexture> GetPanoramaTexture() const
        {
            return res_sky_texture_;
        }
        
    private:
        res::ResourceHandle<gfx::ResTexture> res_sky_texture_;

		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_;
        
        rhi::RefTextureDep generated_cubemap_;
        rhi::RefUavDep generated_cubemap_as_array_uav_;
        rhi::RefSrvDep generated_cubemap_srv_;
    };
}
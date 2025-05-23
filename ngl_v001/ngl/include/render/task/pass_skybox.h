#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"
#include "gfx/render/global_render_resource.h"

namespace ngl::render::task
{

    class PassSkybox : public  rtg::IGraphicsTaskNode
    {
    public:
        enum class EDebugMode
        {
            None,
            SrcCubemap,
            IblSpecular,
            IblDiffuse,
        };
        
        struct SetupDesc
        {
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};

            // Skyのパノラマテクスチャ.
            res::ResourceHandle<gfx::ResTexture>    res_skybox_panorama_texture{};

            // SkyのCubemap版.
            rhi::RefSrvDep  cubemap_srv{};
            // Diffuse畳み込みIBL Cubemap.
            rhi::RefSrvDep  ibl_diffuse_cubemap_srv{};
            // Ggx Specular畳み込みIBL Cubemap.
            rhi::RefSrvDep  ibl_ggx_specular_cubemap_srv{};

            EDebugMode  debug_mode = EDebugMode::None;
            float       debug_mip_bias = 0.0f;
        };
        SetupDesc setup_desc_{};
        
        rtg::RtgResourceHandle h_depth_{};
        rtg::RtgResourceHandle h_light_{};
        
        rhi::RhiRef<rhi::GraphicsPipelineStateDep> pso_;
        
        bool Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
            const SetupDesc& setup_desc,
            rtg::RtgResourceHandle h_depth,
            rtg::RtgResourceHandle h_light
        )
        {
            setup_desc_ = setup_desc;
            {
                if (h_depth.IsInvalid())
                {
                    rtg::RtgResourceDesc2D res_desc{};
                    res_desc.SetupAsAbsoluteSize(setup_desc_.w, setup_desc_.h, rhi::EResourceFormat::Format_D32_FLOAT);
                    h_depth = builder.CreateResource(res_desc);
                }
                h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::DEPTH_TARGET);
            
                if (h_light.IsInvalid())
                {
                    rtg::RtgResourceDesc2D res_desc{};
                    res_desc.SetupAsAbsoluteSize(setup_desc_.w, setup_desc_.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
                    h_light = builder.CreateResource(res_desc);
                }
                h_light_ = builder.RecordResourceAccess(*this, h_light, rtg::access_type::RENDER_TARGET);
            }

            {
                // 初期化. シェーダバイナリの要求とPSO生成.
				
                auto& res_mgr = ngl::res::ResourceManager::Instance();

                ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
                {
                    loaddesc_vs.entry_point_name = "main_vs";
                    loaddesc_vs.stage = ngl::rhi::EShaderStage::Vertex;
                    loaddesc_vs.shader_model_version = k_shader_model;
                }
                // ReverseZで最遠方(Z=0)のフルスクリーントライアングル描画.
                auto res_shader_vs = res_mgr.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("screen/fullscr_procedural_z0_vs.hlsl"), &loaddesc_vs);

                ngl::gfx::ResShader::LoadDesc loaddesc_ps = {};
                {
                    loaddesc_ps.entry_point_name = "main_ps";
                    loaddesc_ps.stage = ngl::rhi::EShaderStage::Pixel;
                    loaddesc_ps.shader_model_version = k_shader_model;
                }
                auto res_shader_ps = res_mgr.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("skybox_pass_ps.hlsl"), &loaddesc_ps);

                ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
                {
                    desc.vs = &res_shader_vs->data_;
                    desc.ps = &res_shader_ps->data_;
                    {
                        desc.num_render_targets = 1;
                        desc.render_target_formats[0] = builder.GetResourceHandleDesc(h_light_).desc.format;
                    }
                    {
                        desc.depth_stencil_format = builder.GetResourceHandleDesc(h_depth_).desc.format;
                        
                        desc.depth_stencil_state.depth_enable = true;
                        desc.depth_stencil_state.depth_write_enable = false;
			            desc.depth_stencil_state.stencil_enable = false;
                        desc.depth_stencil_state.depth_func = rhi::ECompFunc::GreaterEqual;// ReverseZでPreZ描画されていない最遠方ピクセルのみ描画.
                    }
                }
                pso_ = new rhi::GraphicsPipelineStateDep();
                if (!pso_->Initialize(p_device, desc))
                {
                    assert(false);
                    return false;
                }
            }

            builder.RegisterTaskNodeRenderFunction(this,
                [this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
                {
                    command_list_allocator.Alloc(1);
                    auto* command_list = command_list_allocator.GetOrCreate_Front();
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "SkyBox_Screen")
                    
                    auto& global_res = gfx::GlobalRenderResource::Instance();
			        auto* p_cb_pool = command_list->GetDevice()->GetConstantBufferPool();
						
                    // ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
                    auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    auto res_light = builder.GetAllocatedResource(this, h_light_);


                    rhi::ShaderResourceViewDep* cube_srv = setup_desc_.cubemap_srv.Get();
                    bool is_panorama_mode = true;
                    if (EDebugMode::SrcCubemap == setup_desc_.debug_mode)
                    {
                        cube_srv = setup_desc_.cubemap_srv.Get();
                        is_panorama_mode = false;
                    }
                    else if (EDebugMode::IblDiffuse == setup_desc_.debug_mode)
                    {
                        cube_srv = setup_desc_.ibl_diffuse_cubemap_srv.Get();
                        is_panorama_mode = false;
                    }
                    else if (EDebugMode::IblSpecular == setup_desc_.debug_mode)
                    {
                        cube_srv = setup_desc_.ibl_ggx_specular_cubemap_srv.Get();
                        is_panorama_mode = false;
                    }
                    
                    struct CbSkyBox
                    {
                        float	exposure;
                        u32     panorama_mode;
                        float   debug_mip_bias;
                    };
                    auto cbh = p_cb_pool->Alloc(sizeof(CbSkyBox));
                    if (auto map_ptr = cbh->buffer_.MapAs<CbSkyBox>())
                    {
                        map_ptr->exposure = 1.0f;
                        map_ptr->panorama_mode = is_panorama_mode;;
                        map_ptr->debug_mip_bias = setup_desc_.debug_mip_bias;
                        
                        cbh->buffer_.Unmap();
                    }
                    
                    // Viewport.
                    gfx::helper::SetFullscreenViewportAndScissor(command_list, res_light.tex_->GetWidth(), res_light.tex_->GetHeight());

                    // Rtv, Dsv セット.
                    {
                        const auto* p_rtv = res_light.rtv_.Get();
                        command_list->SetRenderTargets(&p_rtv, 1, res_depth.dsv_.Get());
                    }

                    command_list->SetPipelineState(pso_.Get());
                    ngl::rhi::DescriptorSetDep desc_set = {};

                    pso_->SetView(&desc_set, "ngl_cb_sceneview", &setup_desc_.scene_cbv->cbv_);
                    pso_->SetView(&desc_set, "cb_skybox", &cbh->cbv_);
                    pso_->SetView(&desc_set, "tex_skybox_cube", cube_srv);
                    
                    pso_->SetView(&desc_set, "tex_skybox_panorama", setup_desc_.res_skybox_panorama_texture->ref_view_.Get());
                    pso_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
						
                    command_list->SetDescriptorSet(pso_.Get(), &desc_set);

                    command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                    command_list->DrawInstanced(3, 1, 0, 0);
                    
                });
            
            return true;
        }
    private:
    };

    
}

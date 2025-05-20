#pragma once

#include "gfx/render/global_render_resource.h"
#include "render/task/pass_common.h"
#include "resource/resource_manager.h"

#include "framework/gfx_render_command_manager.h"

#include "gfx/command_helper.h"

namespace ngl::gfx::scene
{
    /**
     * @class SkyBox
     *
     * SkyBoxクラスはスカイボックスの生成と初期化を担当し、主にパノラマテクスチャから生成します。
     * パノラマからキューブマップを生成し、拡散畳み込みを行い、関連する描画やリソースタスクを
     * 管理する機能を提供します。
     */
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
            auto FuncCreateCubemapResources = [p_device](
                u32 resolution, u32 mip_count,
                rhi::RefTextureDep* out_cubemap,
                rhi::RefSrvDep* out_mip0_srv = {},
                rhi::RefUavDep* out_mip0_uav = {}
                )
            {
                constexpr u32 k_cubemap_plane_count = 6;

                // サイズは二の冪に限定するためチェック.
                assert((resolution != 0) && (resolution & (resolution - 1)) == 0);

                // Textureは必須.
                assert(out_cubemap);

                const u32 log_2_reso = ngl::MostSignificantBit32(resolution);
                assert(0 < log_2_reso);
                const u32 max_mip_count = log_2_reso + 1;

                u32 gen_mip_count = std::min(mip_count, max_mip_count);
                if (0 == gen_mip_count)
                {
                    gen_mip_count = max_mip_count;
                }
                
                const bool is_need_uav = (nullptr != out_mip0_uav);
                
                (*out_cubemap) = new rhi::TextureDep();
                {
                    rhi::TextureDep::Desc cubemap_desc{};
                    rhi::TextureDep::Desc::InitializeAsCubemap(cubemap_desc, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT, resolution, resolution, false, is_need_uav);
                    // MipCount設定.
                    cubemap_desc.mip_count = gen_mip_count
                    ;
                    if (!(*out_cubemap)->Initialize(p_device, cubemap_desc))
                        assert(false);
                }

                if (out_mip0_srv)
                {
                    (*out_mip0_srv) = new rhi::ShaderResourceViewDep();
                    if (!(*out_mip0_srv)->InitializeAsTexture(p_device, (*out_cubemap).Get(), 0, gen_mip_count, 0, 1))
                        assert(false);
                }
                // UavはMip0について作成.
                if (out_mip0_uav)
                {
                    (*out_mip0_uav) = new rhi::UnorderedAccessViewDep();
                    if (!(*out_mip0_uav)->InitializeRwTexture(p_device, (*out_cubemap).Get(), 0, 0, k_cubemap_plane_count))
                        assert(false);
                }
            };

            // Panoramaから生成するCubemap. エイリアシング対策のためにMip生成.
            FuncCreateCubemapResources(512, 0, &generated_cubemap_, &generated_cubemap_plane_array_srv_, &generated_cubemap_plane_array_uav_);

            // Diffuse Conv Cubemap. Mip無し. 最終的にはTextureではなくSHにしてしまいたい.
            FuncCreateCubemapResources(64, 1, &conv_diffuse_cubemap_, &conv_diffuse_cubemap_pnale_array_srv_, &conv_diffuse_cubemap_plane_array_uav_);
            
            // GGX Specular Conv Cubemap. 全Miplevel.
            FuncCreateCubemapResources(512, 0, &conv_ggx_specular_cubemap_, &conv_ggx_specular_cubemap_pnale_array_srv_, &conv_ggx_specular_cubemap_plane_array_uav_);

            
            
            // PSOセットアップ.
            {
                pso_panorama_to_cube_ = new rhi::ComputePipelineStateDep();
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
                    pso_desc.cs = &res_shader->data_;

                    if (!pso_panorama_to_cube_->Initialize(p_device, pso_desc))
                        assert(false);
                }
                
                pso_conv_cube_diffuse_ = new rhi::ComputePipelineStateDep();
                {
                    gfx::ResShader::LoadDesc loaddesc{};
                    {
                        loaddesc.entry_point_name = "main";
                        loaddesc.stage = ngl::rhi::EShaderStage::Compute;
                        loaddesc.shader_model_version = "6_3";
                    }
                    auto res_shader = res_mgr.LoadResource<gfx::ResShader>(p_device,
                                                NGL_RENDER_SHADER_PATH("util/conv_cubemap_diffuse_cs.hlsl"),
                                                &loaddesc);
                
                    rhi::ComputePipelineStateDep::Desc pso_desc{};
                    pso_desc.cs = &res_shader->data_;

                    if (!pso_conv_cube_diffuse_->Initialize(p_device, pso_desc))
                        assert(false);
                }
                
                pso_conv_cube_ggx_specular_ = new rhi::ComputePipelineStateDep();
                {
                    gfx::ResShader::LoadDesc loaddesc{};
                    {
                        loaddesc.entry_point_name = "main";
                        loaddesc.stage = ngl::rhi::EShaderStage::Compute;
                        loaddesc.shader_model_version = "6_3";
                    }
                    auto res_shader = res_mgr.LoadResource<gfx::ResShader>(p_device,
                                                NGL_RENDER_SHADER_PATH("util/conv_cubemap_ggx_specular_cs.hlsl"),
                                                &loaddesc);
                
                    rhi::ComputePipelineStateDep::Desc pso_desc{};
                    pso_desc.cs = &res_shader->data_;

                    if (!pso_conv_cube_ggx_specular_->Initialize(p_device, pso_desc))
                        assert(false);
                }
                
            }

            // Cubemap生成のRenderCommand登録.
            const rhi::EResourceState cubemap_init_state = rhi::EResourceState::Common;
            ngl::fwk::PushCommonRenderCommand([this, cubemap_init_state](ngl::fwk::ComonRenderCommandArg arg)
            {
                auto& global_res = gfx::GlobalRenderResource::Instance();
                auto* command_list = arg.command_list;
                auto* device = command_list->GetDevice();
                
                NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "Generate_Sky_Cubemap")

                // Panorama to Cubemap.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "Panorama_to_Cubemap")
                   constexpr u32 k_cubemap_plane_count = 6;
                    
                    // UAVステートへ.
                   command_list->ResourceBarrier(generated_cubemap_.Get(), cubemap_init_state, rhi::EResourceState::UnorderedAccess);
                
                   rhi::DescriptorSetDep descset{};
                   pso_panorama_to_cube_->SetView(&descset, "tex_panorama", res_sky_texture_->ref_view_.Get());
                   pso_panorama_to_cube_->SetView(&descset, "samp", global_res.default_resource_.sampler_linear_clamp.Get());
                   pso_panorama_to_cube_->SetView(&descset, "uav_cubemap_as_array", generated_cubemap_plane_array_uav_.Get());
                        
                   command_list->SetPipelineState(pso_panorama_to_cube_.Get());
                   command_list->SetDescriptorSet(pso_panorama_to_cube_.Get(), &descset);
                   pso_panorama_to_cube_->DispatchHelper(command_list, generated_cubemap_->GetWidth(), generated_cubemap_->GetHeight(), k_cubemap_plane_count);

                   // UAVからSrvステートへ.
                   command_list->ResourceBarrier(generated_cubemap_.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::ShaderRead);
                }

                
                // Cubemap Mip生成.
                {
                    // Mipmap生成.
                    ngl::gfx::helper::GenerateCubemapMipmapCompute(command_list, generated_cubemap_.Get(), rhi::EResourceState::ShaderRead,
                        global_res.default_resource_.sampler_linear_clamp.Get(), 1, generated_cubemap_->GetMipCount()-1);
                    
                }

                
                // Diffuse IBL Cubemap畳み込み.
                {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "Conv_Diffuse")
                    constexpr u32 k_cubemap_plane_count = 6;

                    struct CbConvCubemapDiffuse
                    {
                        // ソースのCubemapの解像度に対して畳み込みサンプリングのアンダーサンプリングを緩和するためにMipmapを利用する.
                        u32 use_mip_to_prevent_undersampling;
                    };
                    auto cbh = device->GetConstantBufferPool()->Alloc(sizeof(CbConvCubemapDiffuse));
                    if (auto* map_ptr = cbh->buffer_.MapAs<CbConvCubemapDiffuse>())
                    {
                        map_ptr->use_mip_to_prevent_undersampling = 1;// Mipによるエイリアシング抑制有効.
                        
                        cbh->buffer_.Unmap();
                    }
                    
                    // UAVステートへ.
                   command_list->ResourceBarrier(conv_diffuse_cubemap_.Get(), cubemap_init_state, rhi::EResourceState::UnorderedAccess);
                
                   rhi::DescriptorSetDep descset{};
                    pso_conv_cube_diffuse_->SetView(&descset, "cb_conv_cubemap_diffuse", &cbh->cbv_);
                   pso_conv_cube_diffuse_->SetView(&descset, "tex_cube", generated_cubemap_plane_array_srv_.Get());
                   pso_conv_cube_diffuse_->SetView(&descset, "samp", global_res.default_resource_.sampler_linear_wrap.Get());
                   pso_conv_cube_diffuse_->SetView(&descset, "uav_cubemap_as_array", conv_diffuse_cubemap_plane_array_uav_.Get());
                        
                   command_list->SetPipelineState(pso_conv_cube_diffuse_.Get());
                   command_list->SetDescriptorSet(pso_conv_cube_diffuse_.Get(), &descset);
                   pso_conv_cube_diffuse_->DispatchHelper(command_list, conv_diffuse_cubemap_->GetWidth(), conv_diffuse_cubemap_->GetHeight(), k_cubemap_plane_count);

                   // UAVからSrvステートへ.
                   command_list->ResourceBarrier(conv_diffuse_cubemap_.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::ShaderRead);
                }
                   
               // GGX Specular IBL Cubemap畳み込み. 現状はMip0のみ.
               {
                    NGL_RHI_GPU_SCOPED_EVENT_MARKER(command_list, "Conv_GGX_Specular")
                    constexpr u32 k_cubemap_plane_count = 6;
                   
                    struct CbConvCubemapGgxSpecular
                    {
                        // ソースのCubemapの解像度に対して畳み込みサンプリングのアンダーサンプリングを緩和するためにMipmapを利用する.
                        u32 use_mip_to_prevent_undersampling;
                        float roughness;
                    };
                    auto cbh = device->GetConstantBufferPool()->Alloc(sizeof(CbConvCubemapGgxSpecular));
                    if (auto* map_ptr = cbh->buffer_.MapAs<CbConvCubemapGgxSpecular>())
                    {
                        map_ptr->use_mip_to_prevent_undersampling = 1;// Mipによるエイリアシング抑制有効.
                        map_ptr->roughness = 0.2f;
                        
                        cbh->buffer_.Unmap();
                    }
                    
                    // UAVステートへ.
                   command_list->ResourceBarrier(conv_ggx_specular_cubemap_.Get(), cubemap_init_state, rhi::EResourceState::UnorderedAccess);
                                   
                   rhi::DescriptorSetDep descset{};
                   pso_conv_cube_ggx_specular_->SetView(&descset, "cb_conv_cubemap_ggx_specular", &cbh->cbv_);
                   pso_conv_cube_ggx_specular_->SetView(&descset, "tex_cube", generated_cubemap_plane_array_srv_.Get());
                   pso_conv_cube_ggx_specular_->SetView(&descset, "samp", global_res.default_resource_.sampler_linear_wrap.Get());
                   pso_conv_cube_ggx_specular_->SetView(&descset, "uav_cubemap_as_array", conv_ggx_specular_cubemap_plane_array_uav_.Get());
                        
                   command_list->SetPipelineState(pso_conv_cube_ggx_specular_.Get());
                   command_list->SetDescriptorSet(pso_conv_cube_ggx_specular_.Get(), &descset);
                   pso_conv_cube_ggx_specular_->DispatchHelper(command_list, conv_ggx_specular_cubemap_->GetWidth(), conv_ggx_specular_cubemap_->GetHeight(), k_cubemap_plane_count);
                   
                   // UAVからSrvステートへ.
                   command_list->ResourceBarrier(conv_ggx_specular_cubemap_.Get(), rhi::EResourceState::UnorderedAccess, rhi::EResourceState::ShaderRead);
                }
                
            });
            
            return res_sky_texture_.IsValid();
        }

        
        rhi::RefTextureDep GetCubemap() const
        {
            return generated_cubemap_;
        }
        rhi::RefSrvDep GetCubemapSrv() const
        {
            return generated_cubemap_plane_array_srv_;
        }
        rhi::RefTextureDep GetConvDiffuseCubemap() const
        {
            return conv_diffuse_cubemap_;
        }
        rhi::RefSrvDep GetConvDiffuseCubemapSrv() const
        {
            return conv_diffuse_cubemap_pnale_array_srv_;
        }
        rhi::RefTextureDep GetConvGgxSpecularCubemap() const
        {
            return conv_ggx_specular_cubemap_;
        }
        rhi::RefSrvDep GetConvGgxSpecularCubemapSrv() const
        {
            return conv_ggx_specular_cubemap_pnale_array_srv_;
        }
        
        // パノラマ 基本的には確認用.
        res::ResourceHandle<gfx::ResTexture> GetPanoramaTexture() const
        {
            return res_sky_texture_;
        }
        
    private:
        // HDR Sky Panorama Texture.
        //  扱いやすさや入手のしやすさから空のHDRイメージはパノラマテクスチャをソースとする.
        res::ResourceHandle<gfx::ResTexture> res_sky_texture_;

		rhi::RhiRef<rhi::ComputePipelineStateDep> pso_panorama_to_cube_;
        rhi::RhiRef<rhi::ComputePipelineStateDep> pso_conv_cube_diffuse_;
        rhi::RhiRef<rhi::ComputePipelineStateDep> pso_conv_cube_ggx_specular_;

        // Mipmap有りのSky Cubemap. Panoramaイメージから生成される.
        rhi::RefTextureDep generated_cubemap_;
        rhi::RefUavDep generated_cubemap_plane_array_uav_;
        rhi::RefSrvDep generated_cubemap_plane_array_srv_;

        // Sky Cubemapから畳み込みで生成されるDiffuse IBL Cubemap.
        rhi::RefTextureDep conv_diffuse_cubemap_;
        rhi::RefUavDep conv_diffuse_cubemap_plane_array_uav_;
        rhi::RefSrvDep conv_diffuse_cubemap_pnale_array_srv_;
        
        // Sky Cubemapから畳み込みで生成されるGGX Specular IBL Cubemap.
        rhi::RefTextureDep conv_ggx_specular_cubemap_;
        rhi::RefUavDep conv_ggx_specular_cubemap_plane_array_uav_;
        rhi::RefSrvDep conv_ggx_specular_cubemap_pnale_array_srv_;
    };
}
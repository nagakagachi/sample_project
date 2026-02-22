/*
    global_render_resource.cpp
*/
#include "gfx/rendering/global_render_resource.h"

#include "resource/resource_manager.h"

namespace ngl::gfx
{

        // ランタイム生成デフォルトテクスチャ生成.
        //  RTVのAlignmentがもったいない気がしたので, プログラムでイメージデータをUploadするResTextureにしてみた.
        auto CreateRuntimeDefaultTexture(rhi::DeviceDep* p_device,  const char* name, const math::Vec4& color)
        {
            ResTexture::LoadDesc load_desc = {};
            {
                load_desc.mode = ResTexture::FROM_DESC;
                load_desc.from_desc.type = rhi::ETextureType::Texture2D;
                load_desc.from_desc.format = rhi::EResourceFormat::Format_R8G8B8A8_UNORM;
                load_desc.from_desc.width = 64;
                load_desc.from_desc.height = 64;
                load_desc.from_desc.depth = 1;
                load_desc.from_desc.mip_count = 1;
                load_desc.from_desc.array_size = 1;

                const u32 img_row_byte_size = sizeof(u32) * load_desc.from_desc.width;
                u32 img_slice_byte_size = img_row_byte_size * load_desc.from_desc.height;
                u32 img_byte_size = img_slice_byte_size * load_desc.from_desc.depth;
                
                // Subresource配列. デフォルトテクスチャなのでMip無し.
                load_desc.from_desc.upload_subresource_info_array.resize(1);
                // イメージデータメモリ.
                load_desc.from_desc.upload_pixel_memory_.resize(img_byte_size);
                // イメージデータ作成. ピクセル毎に書き込み(formatに注意).
                {
                    u32 color_rgba8 = (static_cast<u8>(255 * color.x)) | (static_cast<u8>(255 * color.y) << 8) | (static_cast<u8>(255 * color.z) << 16) | (static_cast<u8>(255 * color.w) << 24);
                    
                    u8* p_img = load_desc.from_desc.upload_pixel_memory_.data();
                    for(u32 bi = 0; bi < load_desc.from_desc.upload_pixel_memory_.size(); bi += 4)
                    {
                        *(u32*)(p_img + bi) = color_rgba8;
                    }
                }
                // Subresource設定.
                {
                    auto& subresource_info = load_desc.from_desc.upload_subresource_info_array[0];// Mip0のみなので.

                    subresource_info.format = load_desc.from_desc.format;
                    subresource_info.mip_index = 0;
                    subresource_info.array_index = 0;
                    subresource_info.slice_index = 0;
                    
                    subresource_info.width = load_desc.from_desc.width;
                    subresource_info.height = load_desc.from_desc.height;

                    subresource_info.rowPitch = img_row_byte_size;
                    subresource_info.slicePitch = img_slice_byte_size;
                    
                    subresource_info.pixels = load_desc.from_desc.upload_pixel_memory_.data();
                }
            }
            
            // FileではなくDescに指定した設定とイメージデータからResTexture生成.
            // ResourceManager管理のため識別名指定が必要.
            return res::ResourceManager::Instance().LoadResource<ResTexture>(p_device, name, &load_desc);
        };
    
    bool GlobalRenderResource::Initialize(rhi::DeviceDep* p_device)
    {
        p_device_ = p_device;

        // Sampler.
        {
            // Linear Wrap.
            {
                ngl::rhi::SamplerDep::Desc samp_desc = {};
                samp_desc.Filter = ngl::rhi::ETextureFilterMode::Min_Linear_Mag_Linear_Mip_Linear;
                samp_desc.AddressU = ngl::rhi::ETextureAddressMode::Repeat;
                samp_desc.AddressV = ngl::rhi::ETextureAddressMode::Repeat;
                samp_desc.AddressW = ngl::rhi::ETextureAddressMode::Repeat;
            
                default_resource_.sampler_linear_wrap.Reset(new rhi::SamplerDep());
                if (!default_resource_.sampler_linear_wrap->Initialize(p_device_, samp_desc))
                {
                    std::cout << "[ERROR] Create rhi::SamplerDep" << std::endl;
                    assert(false);
                    return false;
                }
            }
            // Linear Clamp.
            {
                ngl::rhi::SamplerDep::Desc samp_desc = {};
                samp_desc.Filter = ngl::rhi::ETextureFilterMode::Min_Linear_Mag_Linear_Mip_Linear;
                samp_desc.AddressU = ngl::rhi::ETextureAddressMode::Clamp;
                samp_desc.AddressV = ngl::rhi::ETextureAddressMode::Clamp;
                samp_desc.AddressW = ngl::rhi::ETextureAddressMode::Clamp;
            
                default_resource_.sampler_linear_clamp.Reset(new rhi::SamplerDep());
                if (!default_resource_.sampler_linear_clamp->Initialize(p_device_, samp_desc))
                {
                    std::cout << "[ERROR] Create rhi::SamplerDep" << std::endl;
                    assert(false);
                    return false;
                }
            }
        }
        
        {
            // Comparison Sampler for Shadow.
            ngl::rhi::SamplerDep::Desc samp_desc = {};
            samp_desc.Filter = ngl::rhi::ETextureFilterMode::Comp_Min_Linear_Mag_Linear_Mip_Linear;
            samp_desc.ComparisonFunc = rhi::ECompFunc::Greater;
            samp_desc.AddressU = ngl::rhi::ETextureAddressMode::Clamp;
            samp_desc.AddressV = ngl::rhi::ETextureAddressMode::Clamp;
            samp_desc.AddressW = ngl::rhi::ETextureAddressMode::Clamp;

            {
                // Linear.
                samp_desc.Filter = ngl::rhi::ETextureFilterMode::Comp_Min_Linear_Mag_Linear_Mip_Linear;
                
                default_resource_.sampler_shadow_linear.Reset(new rhi::SamplerDep());
                if (!default_resource_.sampler_shadow_linear->Initialize(p_device_, samp_desc))
                {
                    std::cout << "[ERROR] Create rhi::SamplerDep" << std::endl;
                    assert(false);
                    return false;
                }
            }
            {
                // Point.
                samp_desc.Filter = ngl::rhi::ETextureFilterMode::Comp_Min_Point_Mag_Point_Mip_Point;
                
                default_resource_.sampler_shadow_point.Reset(new rhi::SamplerDep());
                if (!default_resource_.sampler_shadow_point->Initialize(p_device_, samp_desc))
                {
                    std::cout << "[ERROR] Create rhi::SamplerDep" << std::endl;
                    assert(false);
                    return false;
                }
            }
        }

        {
            default_resource_.tex_white =   CreateRuntimeDefaultTexture(p_device_, "default_white", math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
            default_resource_.tex_gray50 = CreateRuntimeDefaultTexture(p_device_,"default_gray50", math::Vec4(0.5f, 0.5f, 0.5f, 1.0f));
            default_resource_.tex_gray50_a50 =   CreateRuntimeDefaultTexture(p_device_,"default_gray50_a50", math::Vec4(0.5f, 0.5f, 0.5f, 0.5f));
            default_resource_.tex_black =   CreateRuntimeDefaultTexture(p_device_,"default_black", math::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
            default_resource_.tex_red =     CreateRuntimeDefaultTexture(p_device_,"default_red", math::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
            default_resource_.tex_green =   CreateRuntimeDefaultTexture(p_device_,"default_green", math::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
            default_resource_.tex_blue =    CreateRuntimeDefaultTexture(p_device_,"default_blue", math::Vec4(0.0f, 0.0f, 1.0f, 1.0f));

            default_resource_.tex_default_normal = CreateRuntimeDefaultTexture(p_device_,"default_normal", math::Vec4(0.5f, 0.5f, 1.0f, 1.0f));
        }
        
        return true;
    }
    
    void GlobalRenderResource::Finalize()
    {
        default_resource_ = {};
    }
}

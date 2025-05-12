#pragma once

#include "resource/resource_manager.h"

namespace ngl::gfx::scene
{
    class SkyBox
    {
    public:
        struct EMode
        {
            enum Type
            {
                INVALID,
                PANORAMA,
                CUBEMAP
            };
        };
        
        SkyBox()
        {
            
        }
        ~SkyBox() = default;

        bool InitializeAsPanorama(rhi::DeviceDep* p_device, const char* sky_testure_file_path)
        {
            mode_ = EMode::Type::PANORAMA;
            gfx::ResTexture::LoadDesc desc{};
            {
                desc.mode = gfx::ResTexture::ECreateMode::FROM_FILE;
            }
            res_sky_texture_ = res::ResourceManager::Instance().LoadResource<gfx::ResTexture>(p_device, sky_testure_file_path, &desc);

            return res_sky_texture_.IsValid();
        }

        
        res::ResourceHandle<gfx::ResTexture> GetTexture() const
        {
            return res_sky_texture_;
        }
        
    private:
        res::ResourceHandle<gfx::ResTexture> res_sky_texture_;
        EMode::Type mode_ = EMode::INVALID;
    };
}
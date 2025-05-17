#pragma once

#include "rhi/d3d12/resource_view.d3d12.h"
#include "util/singleton.h"

#include "resource/resource_manager.h"

namespace ngl::gfx
{
    // レンダリング全体から利用するデフォルトリソースなどを簡易管理する.
    class GlobalRenderResource : public Singleton<GlobalRenderResource>
    {
    public:
        bool Initialize(rhi::DeviceDep* p_device);
        void Finalize();

        rhi::DeviceDep* p_device_ = {};

        struct DefaultReource
        {
            rhi::RefSampDep sampler_linear_wrap = {};
            rhi::RefSampDep sampler_linear_clamp = {};
            rhi::RefSampDep sampler_shadow_point = {};
            rhi::RefSampDep sampler_shadow_linear = {};
            
            res::ResourceHandle<ResTexture> tex_white = {};         // 1.0, 1.0, 1.0, 1.0
            res::ResourceHandle<ResTexture> tex_gray50_a50 = {};    // 0.5, 0.5, 0.5, 0.5
            res::ResourceHandle<ResTexture> tex_gray50 = {};        // 0.5, 0.5, 0.5, 1.0
            res::ResourceHandle<ResTexture> tex_black = {};         // 0.0, 0.0, 0.0, 1.0
            res::ResourceHandle<ResTexture> tex_red = {};
            res::ResourceHandle<ResTexture> tex_green = {};
            res::ResourceHandle<ResTexture> tex_blue = {};
            
            res::ResourceHandle<ResTexture> tex_default_normal = {};
            
        } default_resource_;
    };

}

#pragma once

namespace ngl
{
namespace rhi
{
    class GraphicsCommandListDep;
}

namespace gfx
{
    namespace helper
    {
        // Commandlistに指定サイズのフルスクリーンViewportとScissorを設定する.
        void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int width, int height);
        void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int left, int top, int width, int height);

        // CubemapのMip生成をComputeShaderで実行する.
        //  mip index = start_mip_index から generate_mip_count 個分のMipを生成する.
        //  Cubemap TextureはSRV, UAVを許可して生成されている必要がある.
        //  Cubemap Texture のStateは引数のStateに戻されて完了する.
        bool GenerateCubemapMipmapCompute(rhi::GraphicsCommandListDep* p_command_list, rhi::TextureDep* p_texture, rhi::EResourceState resource_state, rhi::SamplerDep* p_sampler, u32 start_mip_index, u32 generate_mip_count);
        
    }
}
}

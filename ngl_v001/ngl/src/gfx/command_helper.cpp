
#include "gfx/command_helper.h"

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"

#include "gfx/resource/resource_shader.h"

#include "resource/resource_manager.h"

#include "gfx/rtg/rtg_common.h"

namespace ngl
{
namespace gfx
{
    namespace helper
    {
        void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int width, int height)
        {
        	SetFullscreenViewportAndScissor(p_command_list, 0, 0, width, height);
        }
    	
    	void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int left, int top, int width, int height)
        {
        	D3D12_VIEWPORT viewport;
        	viewport.MinDepth = 0.0f;
        	viewport.MaxDepth = 1.0f;
        	
        	viewport.TopLeftX = static_cast<float>(left);
        	viewport.TopLeftY = static_cast<float>(top);
        	viewport.Width = static_cast<float>(width);
        	viewport.Height = static_cast<float>(height);

        	D3D12_RECT scissor_rect;
        	scissor_rect.left = left;
        	scissor_rect.top = top;
        	scissor_rect.right = left + width;
        	scissor_rect.bottom = top + height;

        	p_command_list->SetViewports(1, &viewport);
        	p_command_list->SetScissor(1, &scissor_rect);   
        }
    	bool GenerateCubemapMipmapCompute(rhi::GraphicsCommandListDep* p_command_list, rhi::TextureDep* p_texture, rhi::EResourceState resource_state, rhi::SamplerDep* p_sampler, u32 start_mip_index, u32 generate_mip_count)
        {
            auto FuncGenerateCubemapMip = [](rhi::GraphicsCommandListDep* p_command_list, rhi::ComputePipelineStateDep* pso, rhi::TextureDep* cubemap, rhi::SamplerDep* samp, u32 gen_mip_index)
            {
                constexpr u32 k_cubemap_plane_count = 6;

                assert(gen_mip_index >= 1);// mip1以降が対象.
                assert(gen_mip_index < cubemap->GetMipCount());// Mip数未満チェック.

                NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "Cubemap Mip")

                u32 mip_target_width = std::max(cubemap->GetWidth() >> gen_mip_index, 1u);
                u32 mip_target_height = std::max(cubemap->GetHeight() >> gen_mip_index, 1u);

                // Mip読み込み用のSrvを使い捨てで生成. mipは一つ上.
                rhi::RefSrvDep mip_parent_srv(new rhi::ShaderResourceViewDep());
                if (!mip_parent_srv->InitializeAsTexture(p_command_list->GetDevice(), cubemap, gen_mip_index-1, 1, 0, k_cubemap_plane_count))
                    assert(false);

                // Mip書き込み用のUavを使い捨てで生成.
                rhi::RefUavDep mip_uav(new rhi::UnorderedAccessViewDep());
                if (!mip_uav->InitializeRwTexture(p_command_list->GetDevice(), cubemap, gen_mip_index, 0, k_cubemap_plane_count))
                    assert(false);

                
                // UAVステートにしてから呼び出すものとする.
                rhi::DescriptorSetDep descset{};
                pso->SetView(&descset, "tex_cube_mip_parent", mip_parent_srv.Get());
                pso->SetView(&descset, "samp", samp);
                pso->SetView(&descset, "uav_cubemap_mip_as_array", mip_uav.Get());

                p_command_list->SetPipelineState(pso);
                p_command_list->SetDescriptorSet(pso, &descset);
                pso->DispatchHelper(p_command_list, mip_target_width, mip_target_height, k_cubemap_plane_count);

                // UAVバリアだけ発行.
                p_command_list->ResourceUavBarrier(cubemap);
            };

        	assert(rhi::ETextureType::TextureCube == p_texture->GetType());
        	assert(start_mip_index >= 1);
			assert(start_mip_index+generate_mip_count <= p_texture->GetMipCount());

			// Mip生成用Compute PSO.
        	rhi::RhiRef<rhi::ComputePipelineStateDep> pso_gen_cube_mip_(new rhi::ComputePipelineStateDep());
	        {
            	gfx::ResShader::LoadDesc loaddesc{};
	            {
            		loaddesc.entry_point_name = "main";
            		loaddesc.stage = ngl::rhi::EShaderStage::Compute;
            		loaddesc.shader_model_version = "6_3";
	            }
            	auto res_shader = res::ResourceManager::Instance().LoadResource<gfx::ResShader>(p_command_list->GetDevice(),
											NGL_RENDER_SHADER_PATH("util/gen_mip_approx_cubemap_cs.hlsl"),
											&loaddesc);
                
            	rhi::ComputePipelineStateDep::Desc pso_desc{};
            	pso_desc.cs = &res_shader->data_;

                auto* pso_cache = p_command_list->GetDevice()->GetPipelineStateCache();
                pso_gen_cube_mip_ = pso_cache->GetOrCreate(p_command_list->GetDevice(), pso_desc);
	        }
        	
        	// UAV状態で連続呼び出し.
        	p_command_list->ResourceBarrier(p_texture, resource_state, rhi::EResourceState::UnorderedAccess);

        	const u32 gen_mip_count = (start_mip_index+generate_mip_count);
        	for (u32 i = start_mip_index; i < gen_mip_count; ++i)
        	{
        		FuncGenerateCubemapMip(p_command_list, pso_gen_cube_mip_.Get(), p_texture, p_sampler, i);
        	}

        	// ステートを戻す.
        	p_command_list->ResourceBarrier(p_texture, rhi::EResourceState::UnorderedAccess, resource_state);

        	return true;
        }
    } 
}
}

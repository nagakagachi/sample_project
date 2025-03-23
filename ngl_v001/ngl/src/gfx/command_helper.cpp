#pragma once

#include "gfx/command_helper.h"

#include "rhi/d3d12/command_list.d3d12.h"

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
    } 
}
}

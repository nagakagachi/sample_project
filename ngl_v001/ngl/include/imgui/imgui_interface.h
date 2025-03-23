#pragma once

// ImGuiの有効化.
#define NGL_IMGUI_ENABLE 1


#include "util/singleton.h"
#include "rhi/d3d12/device.d3d12.h"

#include "gfx/rtg/graph_builder.h"

namespace ngl::imgui
{
    class ImguiInterface : public Singleton<ImguiInterface>
    {
    public:

        bool Initialize(rhi::DeviceDep* p_device, rhi::SwapChainDep* p_swapchain);
        void Finalize();

        // フレーム開始時.
        bool BeginFrame();
        void EndFrame();

        // ImGuiレンダリングタスクを登録.
        void AppendImguiRenderTask(rtg::RenderTaskGraphBuilder& builder, rtg::RtgResourceHandle h_swapchain);
        
        bool WindProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    private:
        bool initialized_ = false;
        rhi::FrameDescriptorHeapPageInterface descriptor_heap_interface_{};
        
    };
}
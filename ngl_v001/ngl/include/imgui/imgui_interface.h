#pragma once

// ImGuiの有効化.
#define NGL_IMGUI_ENABLE 1

#include <mutex>

#include "../../ngl/external/imgui/imgui.h"

#include "util/singleton.h"
#include "rhi/d3d12/device.d3d12.h"

#include "gfx/rtg/graph_builder.h"

namespace ngl::imgui
{

    // Imgui マルチスレッド向けSnapshot.
    //  https://github.com/ocornut/imgui/issues/1860#issuecomment-1927630727
    struct ImDrawDataSnapshot;
   
    class ImguiInterface : public Singleton<ImguiInterface>
    {
    public:

        bool Initialize(rhi::DeviceDep* p_device, rhi::SwapChainDep* p_swapchain);
        void Finalize();

        // メインスレッド開始.
        bool BeginFrame();
        // メインスレッドにおけるImGui操作終了. RenderThread起動直前のMainThreadとの同期タイミングを想定.
        //  この呼び出しでRenderThread側の描画データアクセスができるようになる.
        void EndFrame();

        // ImGuiレンダリングタスクを登録.
        void AppendImguiRenderTask(rtg::RenderTaskGraphBuilder& builder, rtg::RtgResourceHandle h_swapchain);
        
        bool WindProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    private:
        bool initialized_ = false;
        rhi::FrameDescriptorHeapPageInterface descriptor_heap_interface_{};

        // RenderThreadでのImGuiレンダリングのための描画データSnapshot.
        std::array<ImDrawDataSnapshot*, 2> render_snapshot_{};
        int snapshot_flip_{};
        int snapshot_flip_render_{};
    };

    // ImGuiのIndent/Unindentをスコープで管理するヘルパークラス.
    class ScopedIndent
    {
    private:
        float indent_ = 0;

    public:
        ScopedIndent(float indent)
        {
            indent_ = indent;
            ImGui::Indent(indent);
        }
        ~ScopedIndent()
        {
            ImGui::Unindent(indent_);
        }
    };
    // https://www.jpcert.or.jp/sc-rules/c-pre05-c.html
    #define NGL_IMGUI_JOIN_AGAIN(a,b) a ## b
    #define NGL_IMGUI_JOIN(a,b) NGL_IMGUI_JOIN_AGAIN(a, b)
    // IMGUIのIndent/Unindentをスコープで管理するヘルパークラス用マクロ.
    //	ex. NGL_IMGUI_SCOPED_INDENT(10.0f);
    #define NGL_IMGUI_SCOPED_INDENT(size) const ngl::imgui::ScopedIndent NGL_IMGUI_JOIN(imgui_scoped_indent , __LINE__) (size);

}
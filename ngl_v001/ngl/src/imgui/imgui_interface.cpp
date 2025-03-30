#include "imgui/imgui_interface.h"

#if NGL_IMGUI_ENABLE
// imgui.
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

namespace  ngl::imgui
{
    bool ImguiInterface::Initialize(rhi::DeviceDep* p_device, rhi::SwapChainDep* p_swapchain)
    {
#if NGL_IMGUI_ENABLE
        HWND hwnd = p_device->GetWindow()->Dep().GetWindowHandle();
        auto* p_d3d12_device = p_device->GetD3D12Device();
        int num_swapchain_buffer = p_swapchain->NumResource();
        auto rt_d3d_format = rhi::ConvertResourceFormat(p_swapchain->GetDesc().format);
        // imguiがSRVのDescriptorHeapを要求するため, Heap単位でページ的に確保できるFrameDescriptorHeapPageを利用.
        rhi::FrameDescriptorHeapPageInterface::Desc fdhpi_desc{};
        {
            fdhpi_desc.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        }
        if(!descriptor_heap_interface_.Initialize(p_device->GetFrameDescriptorHeapPagePool(), fdhpi_desc))
        {
            assert(false);
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE d3d_desc_handle_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE d3d_desc_handle_gpu{};
        descriptor_heap_interface_.Allocate(1, d3d_desc_handle_cpu, d3d_desc_handle_gpu);// 一つ確保してPageを取得させる.
        ID3D12DescriptorHeap* d3d_desc_heap = descriptor_heap_interface_.GetD3D12DescriptorHeap();// ページのHeap.

        // ------------------------------------------------------------------------------------------
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(p_d3d12_device, num_swapchain_buffer, rt_d3d_format,
            d3d_desc_heap,
            // You'll need to designate a descriptor from your descriptor heap for Dear ImGui to use internally for its font texture's SRV
            d3d_desc_handle_cpu,
            d3d_desc_handle_gpu
            );
        // ------------------------------------------------------------------------------------------
        
        initialized_ = true;
#endif
        return true;
    }
    void ImguiInterface::Finalize()
    {
#if NGL_IMGUI_ENABLE
        if(!initialized_)
        {
            return;
        }
        
        // ------------------------------------------------------------------------------------------
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        // ------------------------------------------------------------------------------------------

        descriptor_heap_interface_.Finalize();

        initialized_ = false;
#endif
    }

    bool ImguiInterface::BeginFrame()
    {
#if NGL_IMGUI_ENABLE
        if(!initialized_)
        {
            return false;
        }
        
        // ------------------------------------------------------------------------------------------
        // (Your code process and dispatch Win32 messages)
        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // ------------------------------------------------------------------------------------------
        //ImGui::ShowDemoWindow(); // Show demo window! :)
        // ------------------------------------------------------------------------------------------
        
#endif
        return true;
    }

    void ImguiInterface::EndFrame()
    {
#if NGL_IMGUI_ENABLE
        if(!initialized_)
        {
            return;
        }
        
        ImGui::EndFrame();
#endif
        return;
    }
    
    void ImguiInterface::AppendImguiRenderTask(rtg::RenderTaskGraphBuilder& builder, rtg::RtgResourceHandle h_swapchain)
    {
        // ImGuiの描画Task.
        struct TaskImguiRender : public rtg::IGraphicsTaskNode
        {
            ImguiInterface* p_parent_{};
            
            rtg::RtgResourceHandle h_swapchain_{};
			
            void Setup(rtg::RenderTaskGraphBuilder& builder, rtg::RtgResourceHandle h_swapchain, ImguiInterface* p_parent)
            {
                assert(p_parent);
                p_parent_ = p_parent;
                
                // Swapchainの使用を登録.
                h_swapchain_ = builder.RecordResourceAccess(*this, h_swapchain, rtg::access_type::RENDER_TARTGET);
                
                // Render処理のLambdaをRTGに登録.
                builder.RegisterTaskNodeRenderFunction(this,
                    [this](rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* commandlist)
                    {
                        NGL_RHI_GPU_SCOPED_EVENT_MARKER(commandlist, "ImGui");
                            
                        // スケジュール済みリソース(Swapchain)取得.
                        auto res_swapchain = builder.GetAllocatedResource(this, h_swapchain_);
                        assert(res_swapchain.swapchain_.IsValid());
                            
                        #if NGL_IMGUI_ENABLE
                            // ------------------------------------------------------------------------------------------
                            ImGui::Render();
                            // ------------------------------------------------------------------------------------------

                            // ImGui用のDescriptorHeap.
                            ID3D12DescriptorHeap* d3d_desc_heap = p_parent_->descriptor_heap_interface_.GetD3D12DescriptorHeap();
                            D3D12_CPU_DESCRIPTOR_HANDLE rtv_desc_handle_cpu =  res_swapchain.rtv_.Get()->GetD3D12DescriptorHandle();
                            ID3D12GraphicsCommandList* d3d_command_list = commandlist->GetD3D12GraphicsCommandList();

                            // RTV設定.
                            d3d_command_list->OMSetRenderTargets(1, &rtv_desc_handle_cpu, FALSE, nullptr);
                            d3d_command_list->SetDescriptorHeaps(1, &d3d_desc_heap);

                            // ------------------------------------------------------------------------------------------
                            // Imguiレンダリング.
                            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandlist->GetD3D12GraphicsCommandList());
                            // ------------------------------------------------------------------------------------------
                        #endif
                    });
            }
        };
        
        if(!initialized_)
        {
            return;
        }
        // ImGui描画を最後のSwapchainへ描画するTaskを登録.
        auto* task_imgui_to_swapchain = builder.AppendTaskNode<TaskImguiRender>();
        {
            task_imgui_to_swapchain->Setup(builder, h_swapchain, this);
        }
    }
    
    bool ImguiInterface::WindProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
#if NGL_IMGUI_ENABLE
        if(!initialized_)
        {
            return false;
        }
        
        // ------------------------------------------------------------------------------------------
        return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        // ------------------------------------------------------------------------------------------
#else
        return false;
#endif
    }
}

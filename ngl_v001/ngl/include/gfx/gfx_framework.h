#pragma once

#include <vector>
#include <array>

#include "platform/window.h"

#include "thread/job_thread.h"

// rhi
#include "rhi/d3d12/device.d3d12.h"

// resource
#include "resource/resource_manager.h"

#include "gfx/rtg/graph_builder.h"

namespace ngl
{
namespace rhi
{
	class GraphicsCommandListDep;
}
	
struct RtgGenerateCommandListSet
{
	std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> graphics{};
	std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> compute{};
};
	
// Graphicsフレームワーク.
//	Deviceやグラフィックスに関わるフレーム処理などをまとめる.
class GraphicsFramework
{
public:
	GraphicsFramework();
	~GraphicsFramework();

	bool Initialize(ngl::platform::CoreWindow* p_window);
	void Finalize();
	
	// フレームの開始タイミングの処理を担当. MainThread.
	void BeginFrame();
	// フレームのRenderThread同期タイミングで実行する処理を担当. RenderThread.
	void SyncRender();
	// フレームのRender処理の先頭で実行し, また最初にSubmitされるCommandListへの積み込みが必要な処理を担当.
	void BeginFrameRender(std::function< void(std::vector<RtgGenerateCommandListSet>& app_rtg_command_list_set) > app_render_func);
	// Render処理のThread処理を強制的に待機する.
	void ForceWaitFrameRender();
	
	// Submit済みのGPUタスクのすべての完了を待機.
	void WaitAllGpuTask();
	
private:
	// 内部用. フレームのCommandListのSubmit準備として, 以前のSubmitによるGPU処理完了を待機する. RenderThread.
	void ReadyToSubmit();
	// 内部用. フレームのSwapchainのPresent. RenderThread.
	void Present();
	// 内部用. フレームのRender処理完了. RenderThread.
	void EndFrameRender();
	
public:
	ngl::rhi::EResourceState GetSwapchainBufferInitialState() const;

public:
	ngl::rhi::DeviceDep							device_;
	ngl::rhi::GraphicsCommandQueueDep			graphics_queue_;
	ngl::rhi::ComputeCommandQueueDep			compute_queue_;
	
	// SwapChain
	ngl::rhi::RhiRef<ngl::rhi::SwapChainDep>	swapchain_;
	std::vector<ngl::rhi::RefRtvDep>			swapchain_rtvs_;
	ngl::rhi::EResourceState					swapchain_buffer_initial_state_{};
	
	// RenderTaskGraphのCompileやそれらが利用するリソースプール管理.
	ngl::rtg::RenderTaskGraphManager			rtg_manager_{};

	// フレームワークが処理するコマンドを登録するための, Rtgのプールからレンタルしたコマンドリスト参照. フレーム単位プールから取得しているため次のフレームで自動的に返却される.
	ngl::rhi::GraphicsCommandListDep*			p_system_frame_begin_command_list_ = {};
	
private:
	// CommandQueue実行完了待機用Fence
	ngl::rhi::FenceDep							gpu_wait_fence_;
	// CommandQueue実行完了待機用オブジェクト
	ngl::rhi::WaitOnFenceSignalDep				gpu_wait_signal_;
	
	// GPU待機用バッファの最大数. 内部バッファ確保のためのサイズ.
	static constexpr int								k_gpu_work_queue_count_max = 4;
	// SubmitしたGPUタスクのバッファリング待機用情報.
	std::array<ngl::u64, k_gpu_work_queue_count_max>	inflight_gpu_work_id_{};
	std::array<bool, k_gpu_work_queue_count_max>		inflight_gpu_work_id_enable_{};
	// GPU側の待機キューの数. value =< バックバッファ数.
	//	理想的にはバックバッファ数と同じ値だが, 現状RHIのガベージコレクションなどがGPU側1F遅れと想定しているため 1 を指定している.
	//	ガベコレにGPUにSubmitしたフレームIDを識別して破棄する仕組みを入れれば最大限増加してパフォーマンス向上できると考えられる.
	static constexpr ngl::u32							inflight_gpu_work_flip_count_ = 1;
	ngl::u32											inflight_gpu_work_flip_ = 0;
	
	// RenderThread.
	ngl::thread::SingleJobThread				render_thread_;
};

}

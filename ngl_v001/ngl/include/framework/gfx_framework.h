#pragma once

#include <vector>
#include <array>


#include "thread/job_thread.h"
#include "util/ring_buffer.h"

// rhi
#include "rhi/d3d12/device.d3d12.h"

// resource
#include "resource/resource_manager.h"

#include "gfx/rtg/graph_builder.h"

namespace ngl
{
	namespace platform
	{
		class CoreWindow;
	}
	namespace rhi
	{
		class GraphicsCommandListDep;
	}
}
namespace ngl::fwk
{

// フレーム描画の多数のRtg描画によるCommandをリストする.
using RtgFrameRenderSubmitCommandBuffer = std::vector<rtg::RtgSubmitCommandSet>;
	
// Graphicsフレームワーク.
//	Deviceやグラフィックスに関わるフレーム処理などをまとめる.
class GraphicsFramework
{
public:
	GraphicsFramework();
	~GraphicsFramework();

	bool Initialize(platform::CoreWindow* p_window);
	// 終了処理前半. ThreadやGPUタスクの完了待ち.
	void FinalizePrev();
	// 終了処理後半. リソースの破棄.
	void FinalizePost();

	// フレームワークやCoreWindowの有効性チェック.
	bool IsValid() const;
	
	// フレームの開始タイミングの処理を担当. MainThread.
	void BeginFrame();
	// フレームのRenderThread同期タイミングで実行する処理を担当. RenderThread.
	void SyncRender();
	// フレームのRender処理の先頭で実行し, また最初にSubmitされるCommandListへの積み込みが必要な処理を担当.
	void BeginFrameRender(std::function< void(RtgFrameRenderSubmitCommandBuffer& app_rtg_command_list_set) > app_render_func);
	// Render処理のThread処理を強制的に待機する.
	void ForceWaitFrameRender();
	
	// Submit済みのGPUタスクのすべての完了を待機.
	void WaitAllGpuTask();

private:
    void EmptyFrameProcessForDestroy();

public:
	struct Statistics
	{
		u64 device_frame_index{};
		u64 app_render_func_micro_sec{};
		u64 wait_render_thread_micro_sec{};
		u64 wait_gpu_fence_micro_sec{};
		u64 wait_present_micro_sec{};

		bool collected_cpu_render_thread{};// cpu render thread の情報集計が完了したか.
	};
	int NumStatisticsHistoryCount() const;
	Statistics GetStatistics(int history_index) const;
	
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
	ngl::platform::CoreWindow*					p_window_{};
		
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

private:
	StaticSizeRingBuffer<Statistics, 16>		stat_history_{};
	Statistics									stat_on_render_{};
};

}

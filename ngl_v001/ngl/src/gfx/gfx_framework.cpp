
#include "gfx/gfx_framework.h"

#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

#include "gfx/command_helper.h"

// gfx 共通リソース.
#include "gfx/render/global_render_resource.h"

// Imgui.
#include "imgui/imgui_interface.h"

namespace ngl
{

	GraphicsFramework::GraphicsFramework()
	{

	}
	GraphicsFramework::~GraphicsFramework()
	{
		swapchain_.Reset();
		graphics_queue_.Finalize();
		compute_queue_.Finalize();
		device_.Finalize();
	}

	bool GraphicsFramework::Initialize(ngl::platform::CoreWindow* p_window)
	{
		// Graphics Device.
		{
			ngl::rhi::DeviceDep::Desc device_desc{};
#if _DEBUG
			device_desc.enable_debug_layer = true;	// デバッグレイヤ有効化.
#endif
			device_desc.frame_descriptor_size = 500000;
			device_desc.persistent_descriptor_size = 500000;
			if (!device_.Initialize(p_window, device_desc))
			{
				std::cout << "[ERROR] Initialize Device" << std::endl;
				return false;
			}

			// Raytracing Support Check.
			if (!device_.IsSupportDxr())
			{
				MessageBoxA(p_window->Dep().GetWindowHandle(), "Raytracing is not supported on this device.", "Info", MB_OK);
			}

			// GpuWorkId管理バッファサイズチェック.
			assert(k_gpu_work_queue_count_max > device_.GetDesc().swapchain_buffer_count);
			assert(device_.GetDesc().swapchain_buffer_count >= inflight_gpu_work_flip_count_);

		}
		// graphics queue.
		if (!graphics_queue_.Initialize(&device_))
		{
			std::cout << "[ERROR] Initialize Graphics Command Queue" << std::endl;
			return false;
		}
		// compute queue.
		if (!compute_queue_.Initialize(&device_))
		{
			std::cout << "[ERROR] Initialize Compute Command Queue" << std::endl;
			return false;
		}
		// swapchain.
		{
			ngl::rhi::SwapChainDep::Desc swap_chain_desc;
			swap_chain_desc.format = ngl::rhi::EResourceFormat::Format_R10G10B10A2_UNORM;
			swapchain_ = new ngl::rhi::SwapChainDep();
			if (!swapchain_->Initialize(&device_, &graphics_queue_, swap_chain_desc))
			{
				std::cout << "[ERROR] Initialize SwapChain" << std::endl;
				return false;
			}

			// SwapchainBuffer初期ステート保存.
			swapchain_buffer_initial_state_ = ngl::rhi::EResourceState::Common;

			swapchain_rtvs_.resize(swapchain_->NumResource());
			for (auto i = 0u; i < swapchain_->NumResource(); ++i)
			{
				swapchain_rtvs_[i] = new ngl::rhi::RenderTargetViewDep();
				swapchain_rtvs_[i]->Initialize(&device_, swapchain_.Get(), i);
			}
		}
		// GPU待機用Fence.
		if (!gpu_wait_fence_.Initialize(&device_))
		{
			std::cout << "[ERROR] Initialize Fence" << std::endl;
			return false;
		}

		// RTGマネージャ初期化.
		{
			rtg_manager_.Init(&device_, 4);
		}

		// デフォルトテクスチャ等の簡易アクセス用クラス初期化.
		if (!ngl::gfx::GlobalRenderResource::Instance().Initialize(&device_))
		{
			assert(false);
			return false;
		}
	
		// imgui.
		if(!ngl::imgui::ImguiInterface::Instance().Initialize(&device_, swapchain_.Get()))
		{
			std::cout << "[ERROR] Initialize Imgui" << std::endl;
			assert(false);
			return false;
		}
	
		return true;
	}

	void GraphicsFramework::Finalize()
	{
		// RenderThread待機.
		render_thread_.Wait();
		
		// Submit済みのGPUタスク終了待ち.
		WaitAllGpuTask();

		// 共有リソースシステム終了.
		ngl::gfx::GlobalRenderResource::Instance().Finalize();
		
		// imgui.
		ngl::imgui::ImguiInterface::Instance().Finalize();
	}


	// フレームの開始タイミングの処理を担当. MainThread.
	void GraphicsFramework::BeginFrame()
	{
		// imgui.
		ngl::imgui::ImguiInterface::Instance().BeginFrame();
	}
	// フレームのRenderThread同期タイミングで実行する処理を担当. RenderThread.
	void GraphicsFramework::SyncRender()
	{
		// RenderThread完了待機.
		render_thread_.Wait();
		
		// Graphics Deviceのフレーム準備
		device_.ReadyToNewFrame();

		// RTGのフレーム開始処理.
		rtg_manager_.BeginFrame();
		
		// IMGUIのEndFrame呼び出し.
		ngl::imgui::ImguiInterface::Instance().EndFrame();
	}
	// RenderThreadでフレームのシステム及びApp処理を実行する.
	//	ResourceSystem処理, App処理, GPU待機, Submit, Present, 次フレーム準備の一連の処理を実行.
	void GraphicsFramework::BeginFrameRender(std::function< void(RtgFrameRenderSubmitCommandBuffer& app_rtg_command_list_set) > app_render_func)
	{
		// RenderThreadにシステム処理とAPp描画Lambdaを実行させる.
		render_thread_.Begin([this, app_render_func]
		{
			// システム用のフレーム先頭実行コマンドリストをRtgから準備.
			p_system_frame_begin_command_list_ = {};
			rtg_manager_.GetNewFrameCommandList(p_system_frame_begin_command_list_);
			p_system_frame_begin_command_list_->Begin();// begin.

			// ResourceManagerのRenderThread処理.
			// TextureLinearBufferや MeshBufferのUploadなど.
			ngl::res::ResourceManager::Instance().UpdateResourceOnRender(&device_, p_system_frame_begin_command_list_);

			
			// アプリケーション側のRender処理.
			RtgFrameRenderSubmitCommandBuffer app_rtg_command_list_set{};
			app_render_func(app_rtg_command_list_set);
			
			// フレームワークのSubmit準備&前回GPUタスク完了待ち.
			ReadyToSubmit();
			
			// アプリケーションのSubmit.
			for(auto& e : app_rtg_command_list_set)
			{
				ngl::rtg::RenderTaskGraphBuilder::SubmitCommand(graphics_queue_, compute_queue_, e.graphics, e.compute);
			}
			
			// フレームワークのPresent.
			Present();
			// フレームワークのRender終了&次フレームの準備.
			EndFrameRender();
		}
		);
	}
	// Render処理のThread処理を強制的に待機する.
	void GraphicsFramework::ForceWaitFrameRender()
	{
		render_thread_.Wait();
	}
	// フレームのCommandListのSubmit準備として, 以前のSubmitによるGPU処理完了を待機する. RenderThread.
	void GraphicsFramework::ReadyToSubmit()
	{
		// ------------------------------------------------------------------------------------------
		// 今回フレームのコマンドをGPUにSubmitする前に前回のGPU処理完了待機.
		// GPU側にN個のキューを想定している場合は今回使用するキューのタスク完了を待つ(現状は1つ).
		if (inflight_gpu_work_id_enable_[inflight_gpu_work_flip_])
		{
			// 今回のGPUタスク待機バッファの完了を待機.
			gpu_wait_signal_.Wait(&gpu_wait_fence_, inflight_gpu_work_id_[inflight_gpu_work_flip_]);

			inflight_gpu_work_id_enable_[inflight_gpu_work_flip_] = false;
		}
		// ------------------------------------------------------------------------------------------

		// システム用のフレーム先頭実行コマンドリストをSubmit.
		{
			p_system_frame_begin_command_list_->End();
			ngl::rhi::CommandListBaseDep* submit_list[] = { p_system_frame_begin_command_list_ };
			graphics_queue_.ExecuteCommandLists(static_cast<unsigned int>(std::size(submit_list)), submit_list);

			// 念の為クリア. 実体はRtg管理下のフレーム単位プールなので自動的に返却される.
			p_system_frame_begin_command_list_ = {};
		}
	}
	// フレームのSwapchainのPresent. RenderThread.
	void GraphicsFramework::Present()
	{
		swapchain_->GetDxgiSwapChain()->Present(0, 0);
	}
	// フレームのRender処理完了. RenderThread.
	void GraphicsFramework::EndFrameRender()
	{
		const auto submit_gpu_work_id = device_.GetDeviceFrameIndex();

		// 今回のGPUタスクの待機用シグナル発行とそのバッファリング.
		inflight_gpu_work_id_[inflight_gpu_work_flip_] = submit_gpu_work_id;
		graphics_queue_.Signal(&gpu_wait_fence_, submit_gpu_work_id);
		inflight_gpu_work_id_enable_[inflight_gpu_work_flip_] = true;

		inflight_gpu_work_flip_ = (inflight_gpu_work_flip_ + 1) % inflight_gpu_work_flip_count_;
	}

	// Submit済みのGPUタスクのすべての完了を待機.
	void GraphicsFramework::WaitAllGpuTask()
	{
		// SubmitしたすべてのGPUタスクの完了待ち.
		for (ngl::u32 gpu_work_offset = 0; gpu_work_offset < inflight_gpu_work_flip_count_; ++gpu_work_offset)
		{
			const auto gpu_work_index = (inflight_gpu_work_flip_ + gpu_work_offset) % inflight_gpu_work_flip_count_;
			if (inflight_gpu_work_id_enable_[gpu_work_index])
			{
				gpu_wait_signal_.Wait(&gpu_wait_fence_, inflight_gpu_work_id_[gpu_work_index]);
				inflight_gpu_work_id_enable_[gpu_work_index] = false;
			}
		}
	}


	ngl::rhi::EResourceState GraphicsFramework::GetSwapchainBufferInitialState() const
	{
		return swapchain_buffer_initial_state_;
	}
	;

}

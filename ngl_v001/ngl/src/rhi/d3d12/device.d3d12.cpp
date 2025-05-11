
#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"

#include <array>
#include <algorithm>



#ifdef _DEBUG
#include <system_error>
#endif

// lib
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#if 1
// for Agility SDK. NuGetパッケージに対応したバージョン値を以下の公式ページを参考に指定(1.615.1 は 615 となっている).
// https://devblogs.microsoft.com/directx/directx12agility/
// https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 615;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }
#endif


namespace ngl
{
	namespace rhi
	{
		namespace helper
		{
			bool SerializeAndCreateRootSignature(DeviceDep* p_device, const D3D12_ROOT_SIGNATURE_DESC& desc, Microsoft::WRL::ComPtr<ID3D12RootSignature>& out_root_signature)
			{
				auto device = p_device->GetD3D12Device();
				Microsoft::WRL::ComPtr<ID3DBlob> blob;
				Microsoft::WRL::ComPtr<ID3DBlob> error;

				if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
				{
					std::cout << "[ERROR] SerializeRootSignature" << std::endl;
					std::wcout << static_cast<wchar_t*>(error->GetBufferPointer()) << std::endl;
					assert(false);
					return false;
				}
				if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&out_root_signature))))
				{
					std::cout << "[ERROR] CreateRootSignature" << std::endl;
					assert(false);
					return false;
				}
				return true;
			}

			bool SerializeAndCreateRootSignature(Microsoft::WRL::ComPtr<ID3D12RootSignature>& out_root_signature, DeviceDep* p_device, D3D12_ROOT_PARAMETER* p_param_array, uint32_t num_param, D3D12_ROOT_SIGNATURE_FLAGS flag)
			{
				D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
				root_signature_desc.NumParameters = num_param;
				root_signature_desc.pParameters = p_param_array;
				root_signature_desc.Flags = flag; // Global (Not Local).
				return SerializeAndCreateRootSignature(p_device, root_signature_desc, out_root_signature);
			}
			bool SerializeAndCreateLocalRootSignature(Microsoft::WRL::ComPtr<ID3D12RootSignature>& out_root_signature, DeviceDep* p_device, D3D12_ROOT_PARAMETER* p_param_array, uint32_t num_param, D3D12_ROOT_SIGNATURE_FLAGS flag)
			{
				D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
				root_signature_desc.NumParameters = num_param;
				root_signature_desc.pParameters = p_param_array;
				root_signature_desc.Flags = flag | D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE; // Local.
				return SerializeAndCreateRootSignature(p_device, root_signature_desc, out_root_signature);
			}
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		DeviceDep::DeviceDep()
		{
		}
		DeviceDep::~DeviceDep()
		{
			Finalize();
		}

		bool DeviceDep::Initialize(ngl::platform::CoreWindow* window, const Desc& desc)
		{
			if (!window)
				return false;

			desc_ = desc;
			p_window_ = window;

			// DebugLayer有効化
			if (desc_.enable_debug_layer)
			{
				Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
				if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
				{
					debugController->EnableDebugLayer();
				}
			}

			// Factory生成.
			{
#if _DEBUG
				if (desc_.enable_debug_layer)
				{
					// デバッグ情報有効Factory
					if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&p_factory_))))
					{
						// DXGIファクトリ生成失敗.
						std::cout << "[ERROR] Create DXGIFactory" << std::endl;
						return false;
					}
				}
				else
#endif
				{
					// リリース版用Factory
					if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&p_factory_))))
					{
						// DXGIファクトリ生成失敗.
						std::cout << "[ERROR] Create DXGIFactory" << std::endl;
						return false;
					}
				}
			}
			// TODO. アダプタ検索する場合はここでFactoryから.



			D3D_FEATURE_LEVEL feature_levels[] =
			{
				D3D_FEATURE_LEVEL_12_1,
				D3D_FEATURE_LEVEL_12_0,
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
			};

			device_feature_level_ = {};
			device_dxr_tier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
			p_device5_.Reset();

			{
				bool end_create_device = false;

				// Find the HW adapter
				Microsoft::WRL::ComPtr<IDXGIAdapter1> pAdapter;
				for (uint32_t i = 0; !end_create_device && DXGI_ERROR_NOT_FOUND != p_factory_->EnumAdapters1(i, &pAdapter); i++)
				{
					DXGI_ADAPTER_DESC1 desc;
					pAdapter->GetDesc1(&desc);

					// Skip SW adapters
					if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;


					for (auto l : feature_levels)
					{
						if (SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), l, IID_PPV_ARGS(&p_device_))))
						{
							D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
							HRESULT hr = p_device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
							if (SUCCEEDED(hr) && features5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
							{
								// DXR対応ならDeviceInterfaceの問い合わせ.
								if (FAILED(p_device_->QueryInterface(IID_PPV_ARGS(&p_device5_))))
								{
									std::cout << "[ERROR] Failed QueryInterface for ID3D12Device5" << std::endl;
									continue;
								}

								// 目的のDeviceが生成できれば完了.
								device_dxr_tier_ = features5.RaytracingTier;
								device_feature_level_ = l;
								end_create_device = true;

								break;
							}
						}
					}
				}
			}

			// PersistentDescriptorManager初期化
			{
				p_persistent_descriptor_allocator_.reset(new PersistentDescriptorAllocator());
				PersistentDescriptorAllocator::Desc pda_desc = {};
				pda_desc.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				pda_desc.allocate_descriptor_count_ = desc_.persistent_descriptor_size;

				if (!p_persistent_descriptor_allocator_->Initialize(this, pda_desc))
				{
					std::cout << "[ERROR] Create PersistentDescriptorAllocator" << std::endl;
					return false;
				}

				// global変数のdefault descriptor変数に設定(CbvSrvUav用).
				DefaultPersistentDescriptorInfoHolder::SetGlobalDefaultPersistentDescriptor_CbvSrvUav(p_persistent_descriptor_allocator_->GetDefaultPersistentDescriptor());

			}

			// Sampler用PersistentDescriptorManager初期化
			{
				p_persistent_sampler_descriptor_allocator_.reset(new PersistentDescriptorAllocator());
				PersistentDescriptorAllocator::Desc pda_desc = {};
				pda_desc.type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;// Sampler用Heap
				// SamplerのDescriptorHeapは2048個分までの制限がある. Samplerはパラメータの組み合わせがほぼ決まっているので使い切ることは無いはず.
				// また,RootSignature固定化のため無効なレジスタがある場合に設定するダミーDescriptorを一つ確保するため実際に利用できる最大数は -1.
				// と思ったがHeapの確保自体は2048より多くできるようだ. 実際にDescriptorを作成した際に一つのHeap内に2048個までしか同時に存在できないということか?.
				pda_desc.allocate_descriptor_count_ = 2048;
				if (!p_persistent_sampler_descriptor_allocator_->Initialize(this, pda_desc))
				{
					std::cout << "[ERROR] Create PersistentDescriptorAllocator" << std::endl;
					return false;
				}

				// global変数のdefault descriptor変数に設定(Sampler用).
				DefaultPersistentDescriptorInfoHolder::SetGlobalDefaultPersistentDescriptor_Sampler(p_persistent_sampler_descriptor_allocator_->GetDefaultPersistentDescriptor());

			}

			// DynamicDescriptorManager初期化
			{
				p_dynamic_descriptor_manager_.reset(new DynamicDescriptorManager());
				DynamicDescriptorManager::Desc fdm_desc = {};
				fdm_desc.allocate_descriptor_count_ = desc_.frame_descriptor_size;
				fdm_desc.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

				if (!p_dynamic_descriptor_manager_->Initialize(this, fdm_desc))
				{
					std::cout << "[ERROR] Create DynamicDescriptorManager" << std::endl;
					return false;
				}
			}

			// FrameDescriptorHeapPagePool初期化
			{
				p_frame_descriptor_page_pool_.reset(new FrameDescriptorHeapPagePool());
				if (!p_frame_descriptor_page_pool_->Initialize(this))
				{
					std::cout << "[ERROR] Create FrameDescriptorHeapPagePool" << std::endl;
					return false;
				}
			}

			// Gabage Collector.
			{
				if (!gb_.Initialize())
				{
					std::cout << "[ERROR] Initialize RHI GabageCollector" << std::endl;
					return false;
				}
			}

			// ConstantBufferPool.
			{
				cb_pool_.Initialize(this);
			}

			return true;
		}
		void DeviceDep::Finalize()
		{
			cb_pool_.Finalize();
			gb_.Finalize();

			p_device_ = nullptr;
			p_factory_ = nullptr;
		}

		void DeviceDep::ReadyToNewFrame()
		{
			++frame_index_;

			buffer_index_ = (buffer_index_ + 1) % desc_.swapchain_buffer_count;

			p_dynamic_descriptor_manager_->ReadyToNewFrame((u32)frame_index_);

			cb_pool_.ReadyToNewFrame();
			gb_.ReadyToNewFrame();


			// ガベコレ. 実際にはここを別スレッドに逃がすことも考えられる.
			gb_.Execute();
		}

		// 派生Deviceクラスで実装.
		void DeviceDep::DestroyRhiObject(IRhiObject* p)
		{
			gb_.Enqueue(p);
		}


		ngl::platform::CoreWindow* DeviceDep::GetWindow()
		{
			return p_window_;
		}

		ID3D12Device* DeviceDep::GetD3D12Device()
		{
			return p_device_.Get();
		}
		ID3D12Device5* DeviceDep::GetD3D12DeviceForDxr()
		{
			return p_device5_.Get();
		}
		DeviceDep::DXGI_FACTORY_TYPE* DeviceDep::GetDxgiFactory()
		{
			return p_factory_.Get();
		}
		bool DeviceDep::IsSupportDxr() const
		{
			return D3D12_RAYTRACING_TIER_NOT_SUPPORTED != device_dxr_tier_;
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		SwapChainDep::SwapChainDep()
		{
		}
		SwapChainDep::~SwapChainDep()
		{
			Finalize();
		}
		bool SwapChainDep::Initialize(DeviceDep* p_device, GraphicsCommandQueueDep* p_graphics_command_queu, const Desc& desc)
		{
			if (!p_device || !p_graphics_command_queu)
				return false;

			// 依存部からHWND取得
			auto&& window_dep = p_device->GetWindow()->Dep();
			auto&& hwnd = window_dep.GetWindowHandle();
			unsigned int screen_w = 0;
			unsigned int screen_h = 0;
			p_device->GetWindow()->Impl()->GetScreenSize(screen_w, screen_h);

			const auto device_desc = p_device->GetDesc();

			DXGI_SWAP_CHAIN_DESC1 obj_desc = {};
			obj_desc.Width = screen_w;
			obj_desc.Height = screen_h;
			obj_desc.Format = ConvertResourceFormat(desc.format);
			obj_desc.Stereo = false;
			obj_desc.SampleDesc.Count = 1;
			obj_desc.SampleDesc.Quality = 0;
#if 0
			obj_desc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
#else
			// https://learn.microsoft.com/ja-jp/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect
			// https://learn.microsoft.com/ja-jp/windows/win32/direct3ddxgi/dxgi-usage
			obj_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
#endif
			obj_desc.BufferCount = device_desc.swapchain_buffer_count;

			obj_desc.Scaling = DXGI_SCALING_STRETCH;
			obj_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			obj_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

			//obj_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			obj_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;// presentでのDXGI_PRESENT_ALLOW_TEARINGを許可.


			// 一時オブジェクトでSwapchain生成
			IDXGISwapChain1* p_tmp_swap;
			if (FAILED(p_device->GetDxgiFactory()->CreateSwapChainForHwnd(p_graphics_command_queu->GetD3D12CommandQueue(), hwnd, &obj_desc, nullptr, nullptr, &p_tmp_swap)))
			{
				std::cout << "[ERROR] Create Command Queue" << std::endl;
				return false;
			}

			// QueryInterfaceでIDXGISwapChain4要求
			if (FAILED(p_tmp_swap->QueryInterface(IID_PPV_ARGS(&p_swapchain_))))
			{
				return false;
			}
			// 一時オブジェクト破棄
			p_tmp_swap->Release();


			// Resource取得
			num_resource_ = device_desc.swapchain_buffer_count;
			p_resources_ = new Microsoft::WRL::ComPtr<ID3D12Resource> [num_resource_];
			for (auto i = 0u; i < num_resource_; ++i)
			{
				if (FAILED(p_swapchain_->GetBuffer(i, IID_PPV_ARGS(&p_resources_[i]))))
				{
					std::cout << "[ERROR] Get SwapChain Buffer " << i << std::endl;
				}
			}

			desc_ = desc;
			width_ = obj_desc.Width;
			height_ = obj_desc.Height;

			return true;
		}
		void SwapChainDep::Finalize()
		{
			if (p_resources_)
			{
				for (auto i = 0u; i < num_resource_; ++i)
				{
					p_resources_[i] = nullptr;
				}
				delete[] p_resources_;
				p_resources_ = nullptr;
			}
			p_swapchain_ = nullptr;
		}
		SwapChainDep::DXGI_SWAPCHAIN_TYPE* SwapChainDep::GetDxgiSwapChain()
		{
			return p_swapchain_.Get();
		}
		unsigned int SwapChainDep::NumResource() const
		{
			return num_resource_;
		}
		ID3D12Resource* SwapChainDep::GetD3D12Resource(unsigned int index)
		{
			if (num_resource_ <= index)
				return nullptr;
			return p_resources_[index].Get();
		}

		unsigned int SwapChainDep::GetCurrentBufferIndex() const
		{
			return p_swapchain_->GetCurrentBackBufferIndex();
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------




		// -------------------------------------------------------------------------------------------------------------------------------------------------
		CommandQueueBaseDep::CommandQueueBaseDep()
		{
		}
		CommandQueueBaseDep::~CommandQueueBaseDep()
		{
		}
		
		// MEMO. ここでCommandQueue生成時に IGIESW .exe found in whitelist: NO というメッセージがVSログに出力される. 意味と副作用は現状不明.
		bool CommandQueueBaseDep::Initialize(DeviceDep* p_device, D3D12_COMMAND_LIST_TYPE type)
		{
			if (!p_device)
				return false;

			D3D12_COMMAND_QUEUE_DESC desc = {};
			desc.Type = type;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			desc.NodeMask = 0;
			desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

			if (FAILED(p_device->GetD3D12Device()->CreateCommandQueue(&desc, IID_PPV_ARGS(&p_command_queue_))))
			{
				std::cout << "[ERROR] Create Command Queue" << std::endl;
				return false;
			}

			return true;
		}

		void CommandQueueBaseDep::Signal(FenceDep* p_fence, ngl::types::u64 fence_value)
		{
			if (!p_fence)
				return;
			// Signal発行.
			p_command_queue_->Signal( p_fence->GetD3D12Fence(), fence_value);
		}
		// FenceでWait.
		void CommandQueueBaseDep::Wait(FenceDep* p_fence, ngl::types::u64 wait_value)
		{
			if (!p_fence)
				return;
			p_command_queue_->Wait( p_fence->GetD3D12Fence(), wait_value);
		}

		ID3D12CommandQueue* CommandQueueBaseDep::GetD3D12CommandQueue()
		{
			return p_command_queue_.Get();
		}
		
		
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		GraphicsCommandQueueDep::GraphicsCommandQueueDep()
		{
		}
		GraphicsCommandQueueDep::~GraphicsCommandQueueDep()
		{
			Finalize();
		}

		// MEMO. ここでCommandQueue生成時に IGIESW .exe found in whitelist: NO というメッセージがVSログに出力される. 意味と副作用は現状不明.
		bool GraphicsCommandQueueDep::Initialize(DeviceDep* p_device)
		{
			return CommandQueueBaseDep::Initialize(p_device, D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT);
		}

		void GraphicsCommandQueueDep::Finalize()
		{
		}

		void GraphicsCommandQueueDep::ExecuteCommandLists(unsigned int num_command_list, CommandListBaseDep** p_command_lists)
		{
			// 一時バッファに詰める
			std::vector<ID3D12CommandList*> p_command_list_array = {};
			for (auto i = 0u; i < num_command_list; ++i)
			{
				p_command_list_array.push_back(p_command_lists[i]->GetD3D12GraphicsCommandList());
			}
			try
			{
				p_command_queue_->ExecuteCommandLists(num_command_list, &(p_command_list_array[0]));
			}
			catch (...)
			{
				// D3D12で稀に発生するcom_errorをキャッチ
				std::cout << "[ngl][GraphicsCommandQueueDep] ExecuteCommandLists: catch exception." << std::endl;
				OutputDebugString(_T("[ngl][GraphicsCommandQueueDep] ExecuteCommandLists: catch exception."));
			}
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		ComputeCommandQueueDep::ComputeCommandQueueDep()
		{
		}
		ComputeCommandQueueDep::~ComputeCommandQueueDep()
		{
			Finalize();
		}

		// MEMO. ここでCommandQueue生成時に IGIESW .exe found in whitelist: NO というメッセージがVSログに出力される. 意味と副作用は現状不明.
		bool ComputeCommandQueueDep::Initialize(DeviceDep* p_device)
		{
			return CommandQueueBaseDep::Initialize(p_device, D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_COMPUTE);
		}

		void ComputeCommandQueueDep::Finalize()
		{
		}

		void ComputeCommandQueueDep::ExecuteCommandLists(unsigned int num_command_list, CommandListBaseDep** p_command_lists)
		{
			// 一時バッファに詰める
			std::vector<ID3D12CommandList*> p_command_list_array = {};
			for (auto i = 0u; i < num_command_list; ++i)
			{
				p_command_list_array.push_back(p_command_lists[i]->GetD3D12GraphicsCommandList());
			}
			try
			{
				p_command_queue_->ExecuteCommandLists(num_command_list, &(p_command_list_array[0]));
			}
			catch (...)
			{
				// D3D12で稀に発生するcom_errorをキャッチ
				std::cout << "[ngl][ComputeCommandQueueDep] ExecuteCommandLists: catch exception." << std::endl;
				OutputDebugString(_T("[ngl][ComputeCommandQueueDep] ExecuteCommandLists: catch exception."));
			}
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		FenceDep::FenceDep()
		{
		}
		FenceDep::~FenceDep()
		{
			Finalize();
		}

		bool FenceDep::Initialize(DeviceDep* p_device)
		{
			if (!p_device)
				return false;

			if (FAILED(p_device->GetD3D12Device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p_fence_))))
			{
				std::cout << "[ERROR] Create Fence" << std::endl;
				return false;
			}

			return true;
		}
		void FenceDep::Finalize()
		{
			p_fence_ = nullptr;
		}
		ID3D12Fence* FenceDep::GetD3D12Fence()
		{
			return p_fence_.Get();
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		WaitOnFenceSignalDep::WaitOnFenceSignalDep()
		{
			// イベント作成
			win_event_ = CreateEvent(nullptr, false, false, nullptr);
		}
		WaitOnFenceSignalDep::~WaitOnFenceSignalDep()
		{
			// イベント破棄
			CloseHandle(win_event_);
		}

		// 発行したSignalによるFence値が指定した値になるまで待機. プラットフォーム毎に異なる実装. WindowsではEventを利用.
		void WaitOnFenceSignalDep::Wait(FenceDep* p_fence, ngl::types::u64 complete_fence_value)
		{
			if (!p_fence)
				return;

			if (p_fence->GetD3D12Fence()->GetCompletedValue() != complete_fence_value)
			{
				// Fence値とイベントを関連付け
				p_fence->GetD3D12Fence()->SetEventOnCompletion(complete_fence_value, win_event_);

				// イベント発火待機
				WaitForSingleObject(win_event_, INFINITE);
			}
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------
	}
}
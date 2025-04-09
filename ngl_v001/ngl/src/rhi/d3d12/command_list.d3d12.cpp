

#include "rhi/d3d12/command_list.d3d12.h"

#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

#if defined(NGL_ENABLE_GPU_EVENT_MARKER)
// PIX
#include <pix3.h>

#include <stdio.h>
#include <stdarg.h>
#endif

namespace ngl
{
	namespace rhi
	{
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		bool CommandListBaseDep::Initialize(DeviceDep* p_device, const Desc& desc)
		{
			if (!p_device)
				return false;

			desc_ = desc;
			parent_device_ = p_device;

			const D3D12_COMMAND_LIST_TYPE command_list_type = desc.type;

			// Command Allocator
			if (FAILED(p_device->GetD3D12Device()->CreateCommandAllocator(command_list_type, IID_PPV_ARGS(&p_command_allocator_))))
			{
				std::cout << "[ERROR] Create Command Allocator" << std::endl;
				return false;
			}

			// Command List
			if (FAILED(p_device->GetD3D12Device()->CreateCommandList(0, command_list_type, p_command_allocator_.Get(), nullptr, IID_PPV_ARGS(&p_command_list_))))
			{
				std::cout << "[ERROR] Create Command List" << std::endl;
				return false;
			}

			// DXR対応Interfaceを取得(Deviceが対応しているなら).
			p_command_list4_ = nullptr;
			if (p_device->IsSupportDxr())
			{
				if (FAILED(p_command_list_->QueryInterface(IID_PPV_ARGS(&p_command_list4_))))
				{
					std::cout << "[ERROR] QueryInterface for ID3D12GraphicsCommandList4" << std::endl;
				}
			}

			// 初回クローズ. これがないと初回フレームの開始時ResetでComError発生.
			p_command_list_->Close();
			is_open_ = false;// Close状態.

			// フレームでのDescriptor確保用インターフェイス初期化
			FrameCommandListDynamicDescriptorAllocatorInterface::Desc fdi_desc = {};
			fdi_desc.stack_size = 2048;// スタックサイズは適当.
			if (!frame_desc_interface_.Initialize(parent_device_->GeDynamicDescriptorManager(), fdi_desc))
			{
				std::cout << "[ERROR] Create FrameCommandListDynamicDescriptorAllocatorInterface" << std::endl;
				return false;
			}

			// フレームでのSampler Descriptor確保用インターフェイス初期化.
			// SamplerはD3D12ではHeap毎に2048という制限があるため, それを考慮してHeapをPageとして確保して拡張する.
			// こちらはそのままCvbSrvUavにも利用可能.
			FrameDescriptorHeapPageInterface::Desc fdhpi_desc = {};
			fdhpi_desc.type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			if (!frame_desc_page_interface_for_sampler_.Initialize(parent_device_->GetFrameDescriptorHeapPagePool(), fdhpi_desc))
			{
				std::cout << "[ERROR] Create FrameDescriptorHeapPageInterface" << std::endl;
				return false;
			}

			return true;
		}
		void CommandListBaseDep::Begin()
		{
			// 二重Begin禁止.
			assert(!is_open_);
			
			// アロケータリセット
			p_command_allocator_->Reset();
			// コマンドリストリセット
			p_command_list_->Reset(p_command_allocator_.Get(), nullptr);
			is_open_ = true;

			// 新しいフレームのためのFrameDescriptorの準備.
			// インデックスはDeviceから取得するグローバルなフレームインデックス.
			frame_desc_interface_.ReadyToNewFrame((u32)parent_device_->GetDeviceFrameIndex());
		}
		void CommandListBaseDep::End()
		{
			// 二重Close禁止.
			assert(is_open_);
			
			p_command_list_->Close();
			is_open_ = false;
		}
		void CommandListBaseDep::Dispatch(u32 x, u32 y, u32 z)
		{
			p_command_list_->Dispatch(x, y, z);
		}
		
		// UAV Barrier.
		void _UavBarrier(ID3D12GraphicsCommandList* p_command_list, ID3D12Resource* p_resource_uav)
		{
			D3D12_RESOURCE_BARRIER desc = {};
			desc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			desc.Transition.pResource = p_resource_uav;
			p_command_list->ResourceBarrier(1, &desc);
		}
		// UAV同期Barrier.
		void CommandListBaseDep::ResourceUavBarrier(TextureDep* p_texture)
		{
			_UavBarrier(p_command_list_.Get(), p_texture->GetD3D12Resource());
		}
		// UAV同期Barrier.
		void CommandListBaseDep::ResourceUavBarrier(BufferDep* p_buffer)
		{
			_UavBarrier(p_command_list_.Get(), p_buffer->GetD3D12Resource());
		}
		
		void CommandListBaseDep::SetPipelineState(ComputePipelineStateDep* pso)
		{
			p_command_list_->SetPipelineState(pso->GetD3D12PipelineState());
			p_command_list_->SetComputeRootSignature(pso->GetD3D12RootSignature());
		}
		void CommandListBaseDep::SetDescriptorSet(const ComputePipelineStateDep* p_pso, const DescriptorSetDep* p_desc_set)
		{
			assert(p_pso);
			assert(p_desc_set);

			// cbv, srv, uav用デフォルトDescriptor取得.
			auto def_descriptor = parent_device_->GetPersistentDescriptorAllocator()->GetDefaultPersistentDescriptor();
			auto&& resource_table = p_pso->GetPipelineResourceViewLayout()->GetResourceTable();

			const auto& cs_sampler_desc_set_handle_array = p_desc_set->GetCsSampler();
			const auto cs_sampler_table = resource_table.cs_sampler_table;
			// Sampler用のFrameDescriptorHeapに必要な数. 設定された最大のレジスタ番号+1を個数とする.
			auto total_samp_count = cs_sampler_desc_set_handle_array.max_use_register_index + 1;

			// Sampler用のFrameDescriptor確保. ここでPageが足りなければ新規Pageが確保されてHeapが切り替わるので, SetDescriptorHeaps() の前に実行する必要がある.
			const auto sampler_desc_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_sampler_handle_start;
			D3D12_GPU_DESCRIPTOR_HANDLE gpu_sampler_handle_start;
			// Heap確保. 現在のPageで必要分確保できなければ新規Page(Heap)に自動で切り替わる.
			frame_desc_page_interface_for_sampler_.Allocate(total_samp_count, cpu_sampler_handle_start, gpu_sampler_handle_start);
			const u64 sampler_handle_increment_size = frame_desc_page_interface_for_sampler_.GetPool()->GetHandleIncrementSize(sampler_desc_heap_type);

			// DescriptorHeapの設定.
			// Cbv Srv Uav用とSampler用.
			// これ以前にFrameDescriptorから確保して必要ならばHeap切り替えが完了した後にCommandListにHeapを設定する.
			// CommandListにHeapを設定した後にそのHeap上のDescriptorをDescriptorTableに設定する必要がある(設定されているHeapと異なるHeap上のDescriptorをセットするとD3Dエラーとなる.)
			// CbvSrvUavのHeapは巨大な単一Heap上で確保するためアプリケーション実行中に変化しないのでSamplerとは異なりいつ設定しても良い.
			ID3D12DescriptorHeap* heaps[] =
			{
				frame_desc_interface_.GetManager()->GetD3D12DescriptorHeap(),
				frame_desc_page_interface_for_sampler_.GetD3D12DescriptorHeap()
			};
			p_command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);


			// Samplerのコミット.
			{
				// 各ステージのSamplerを設定.
				D3D12_CPU_DESCRIPTOR_HANDLE tmp[k_sampler_table_size];
				auto SetSamplerDescriptor = [&](
					D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
					D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu_handle_start,
					D3D12_GPU_DESCRIPTOR_HANDLE dst_gpu_handle_start,
					u32 src_count, const D3D12_CPU_DESCRIPTOR_HANDLE* src_handle,
					u8 table_index)
				{
					if (0 > table_index || 0 >= src_count)
						return;

					// Copy時に無効なDescriptorがあるとエラーになるため, 無効要素にはダミーのDesctirptorを詰めたバッファを作る.
					//	DescriptorSetDep側で歯抜けの部分にダミーDescriptorを詰めて置くことでそのままコピーできるはずなので, 高速化のため検討.
					for (u32 i = 0; i < src_count; i++)
					{
						tmp[i] = (src_handle[i].ptr > 0) ? src_handle[i] : def_descriptor.cpu_handle;
					}

					// FrameDescriptorHeapから連続したDescriptorを確保してコピー,CommandListへセットする.
					parent_device_->GetD3D12Device()->CopyDescriptors(
						1, &dst_cpu_handle_start, &src_count,
						src_count, tmp, nullptr,
						heap_type);
					p_command_list_->SetComputeRootDescriptorTable(table_index, dst_gpu_handle_start);
				};

				// 指定のFrameDescriptor開始位置から始まる範囲にDescriptorをコピーしてCommandListに設定.
				SetSamplerDescriptor(sampler_desc_heap_type, cpu_sampler_handle_start, gpu_sampler_handle_start, total_samp_count, cs_sampler_desc_set_handle_array.cpu_handles, cs_sampler_table);
			}

			// CBV, SRV, UAVのコミット.
			{
				const auto cvbsrvuav_desc_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

				D3D12_CPU_DESCRIPTOR_HANDLE tmp[k_srv_table_size];// cbv, srv, uav のテーブルサイズで最大の k_srv_table_size でワーク確保して再利用.
				auto SetViewDescriptor = [&](u32 count, const D3D12_CPU_DESCRIPTOR_HANDLE* handles, u8 table_index)
				{
					if (0 > table_index || 0 >= count)
						return;

					// Copy時に無効なDescriptorがあるとエラーになるため, 無効要素にはダミーのDesctirptorを詰めたバッファを作る.
					//	DescriptorSetDep側で歯抜けの部分にダミーDescriptorを詰めて置くことでそのままコピーできるはずなので, 高速化のため検討.
					for (u32 i = 0; i < count; i++)
					{
						tmp[i] = (handles[i].ptr > 0) ? handles[i] : def_descriptor.cpu_handle;
					}

					D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu;
					D3D12_GPU_DESCRIPTOR_HANDLE dst_gpu;
					frame_desc_interface_.Allocate(count, dst_cpu, dst_gpu);

					// FrameDescriptorHeapから連続したDescriptorを確保してコピー,CommandListへセットする.
					parent_device_->GetD3D12Device()->CopyDescriptors(
						1, &dst_cpu, &count,
						count, tmp, nullptr,
						cvbsrvuav_desc_heap_type);
					p_command_list_->SetComputeRootDescriptorTable(table_index, dst_gpu);
				};
				// 各ステージの各リソースタイプ別に連続Descriptorを確保,コピーしてテーブルにをセットしていく
				// 各ステージ毎各リソースタイプ毎に0番から設定された最大レジスタ番号までの範囲でFrameDescriptorから確保してコピー,CommandListへ設定する.
				SetViewDescriptor(p_desc_set->GetCsCbv().max_use_register_index + 1, p_desc_set->GetCsCbv().cpu_handles, resource_table.cs_cbv_table);
				SetViewDescriptor(p_desc_set->GetCsSrv().max_use_register_index + 1, p_desc_set->GetCsSrv().cpu_handles, resource_table.cs_srv_table);
				SetViewDescriptor(p_desc_set->GetCsUav().max_use_register_index + 1, p_desc_set->GetCsUav().cpu_handles, resource_table.cs_uav_table);
			}
		}

		
		void CommandListBaseDep::BeginMarker(const char* format, ...)
		{
#if defined(NGL_ENABLE_GPU_EVENT_MARKER)
			// ここで全て展開.
			char buf[256];
			va_list args;
			va_start( args, format );
			const auto n = vsnprintf( buf, sizeof(buf), format, args );
			va_end( args );
			
			constexpr UINT64 k_color = ~(UINT64(0));
			PIXBeginEvent(GetD3D12GraphicsCommandList(), k_color, buf);
#endif
		}
		void CommandListBaseDep::EndMarker()
		{
#if defined(NGL_ENABLE_GPU_EVENT_MARKER)
			PIXEndEvent(GetD3D12GraphicsCommandList());
#endif
		}
		
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		ComputeCommandListDep::ComputeCommandListDep()
		{
		}
		ComputeCommandListDep::~ComputeCommandListDep()
		{
			Finalize();
		}

		bool ComputeCommandListDep::Initialize(DeviceDep* p_device)
		{
			CommandListBaseDep::Desc base_desc = {};
			{
				base_desc.type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			}
			return CommandListBaseDep::Initialize(p_device, base_desc);
		}
		void ComputeCommandListDep::Finalize()
		{
			
		}
		
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		GraphicsCommandListDep::GraphicsCommandListDep()
		{
		}
		GraphicsCommandListDep::~GraphicsCommandListDep()
		{
			Finalize();
		}
		bool GraphicsCommandListDep::Initialize(DeviceDep* p_device)
		{
			CommandListBaseDep::Desc base_desc = {};
			{
				base_desc.type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			}
			return CommandListBaseDep::Initialize(p_device, base_desc);
		}
		void GraphicsCommandListDep::Finalize()
		{
		}
		void GraphicsCommandListDep::SetRenderTargets(const RenderTargetViewDep** pp_rtv, int num_rtv, const DepthStencilViewDep* p_dsv)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[16];
			assert(std::size(rtvs) >= num_rtv);
			for (auto i = 0; i < num_rtv; ++i)
			{
				rtvs[i] = pp_rtv[i]->GetD3D12DescriptorHandle();
			}

			D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = (p_dsv) ? p_dsv->GetD3D12DescriptorHandle() : D3D12_CPU_DESCRIPTOR_HANDLE();
			const D3D12_CPU_DESCRIPTOR_HANDLE* p_dsv_handle = (p_dsv) ? &dsv_handle : nullptr;
			GetD3D12GraphicsCommandList()->OMSetRenderTargets(num_rtv, rtvs, false, p_dsv_handle);
		};
		void GraphicsCommandListDep::ClearRenderTarget(const RenderTargetViewDep* p_rtv, const float(color)[4])
		{
			auto rtv = p_rtv->GetD3D12DescriptorHandle();
			p_command_list_->ClearRenderTargetView(rtv, color, 0u, nullptr);
		}
		void GraphicsCommandListDep::ClearDepthTarget(const DepthStencilViewDep* p_dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil)
		{
			uint32_t flags = clearDepth ? D3D12_CLEAR_FLAG_DEPTH : 0;
			flags |= clearStencil ? D3D12_CLEAR_FLAG_STENCIL : 0;

			GetD3D12GraphicsCommandList()->ClearDepthStencilView(p_dsv->GetD3D12DescriptorHandle(), D3D12_CLEAR_FLAGS(flags), depth, stencil, 0, nullptr);
		};

		// State Transition Barrier関連共通部.
		void _Barrier(ID3D12GraphicsCommandList* p_command_list, ID3D12Resource* p_resource, EResourceState prev, EResourceState next)
		{
			D3D12_RESOURCE_STATES state_before = ConvertResourceState(prev);
			D3D12_RESOURCE_STATES state_after = ConvertResourceState(next);

			D3D12_RESOURCE_BARRIER desc = {};
			desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			desc.Transition.pResource = p_resource;
			desc.Transition.StateBefore = state_before;
			desc.Transition.StateAfter = state_after;
			// 現状は全サブリソースを対象.
			desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			p_command_list->ResourceBarrier(1, &desc);
		}

		// バリア Swapchain.
		void GraphicsCommandListDep::ResourceBarrier(SwapChainDep* p_swapchain, unsigned int buffer_index, EResourceState prev, EResourceState next)
		{
			if (!p_swapchain || prev == next)
				return;
			auto* resource = p_swapchain->GetD3D12Resource(buffer_index);
			_Barrier(p_command_list_.Get(), resource, prev, next);
		}
		// バリア Texture.
		void GraphicsCommandListDep::ResourceBarrier(TextureDep* p_texture, EResourceState prev, EResourceState next)
		{
			if (!p_texture || prev == next)
				return;
			auto* resource = p_texture->GetD3D12Resource();
			_Barrier(p_command_list_.Get(), resource, prev, next);
		}
		// バリア Buffer.
		void GraphicsCommandListDep::ResourceBarrier(BufferDep* p_buffer, EResourceState prev, EResourceState next)
		{
			if (!p_buffer || prev == next)
				return;
			auto* resource = p_buffer->GetD3D12Resource();
			_Barrier(p_command_list_.Get(), resource, prev, next);
		}

		void GraphicsCommandListDep::SetViewports(u32 num, const  D3D12_VIEWPORT* p_viewports)
		{
			assert(p_viewports);
			assert(num);
			p_command_list_->RSSetViewports( num, p_viewports );
		}
		void GraphicsCommandListDep::SetScissor(u32 num, const  D3D12_RECT* p_rects)
		{
			assert(p_rects);
			assert(num);
			p_command_list_->RSSetScissorRects(num, p_rects);
		}
		void GraphicsCommandListDep::SetPrimitiveTopology(EPrimitiveTopology topology)
		{
			p_command_list_->IASetPrimitiveTopology(ConvertPrimitiveTopology(topology));
		}
		void GraphicsCommandListDep::SetVertexBuffers(u32 slot, u32 num, const D3D12_VERTEX_BUFFER_VIEW* p_views)
		{
			p_command_list_->IASetVertexBuffers( slot, num, p_views );
		}
		void GraphicsCommandListDep::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* p_view)
		{
			p_command_list_->IASetIndexBuffer(p_view);
		}
		
		void GraphicsCommandListDep::DrawInstanced(u32 num_vtx, u32 num_instance, u32 offset_vtx, u32 offset_instance)
		{
			p_command_list_->DrawInstanced(num_vtx, num_instance, offset_vtx, offset_instance);
		}
		void GraphicsCommandListDep::DrawIndexedInstanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, s32  base_vertex_location, u32 start_instance_location)
		{
			p_command_list_->DrawIndexedInstanced(index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
		}

		void GraphicsCommandListDep::SetPipelineState(GraphicsPipelineStateDep* pso)
		{
			p_command_list_->SetPipelineState(pso->GetD3D12PipelineState());
			p_command_list_->SetGraphicsRootSignature(pso->GetD3D12RootSignature());
		}
		void GraphicsCommandListDep::SetDescriptorSet(const GraphicsPipelineStateDep* p_pso, const DescriptorSetDep* p_desc_set)
		{
			assert(p_pso);
			assert(p_desc_set);

			// cbv, srv, uav用デフォルトDescriptor取得.
			auto def_descriptor = parent_device_->GetPersistentDescriptorAllocator()->GetDefaultPersistentDescriptor();
			auto&& resource_table = p_pso->GetPipelineResourceViewLayout()->GetResourceTable();

			struct DescriptorSetInfo
			{
				DescriptorSetInfo(int max_register, const D3D12_CPU_DESCRIPTOR_HANDLE* p_handle, s8 table_index)
					: max_register_(max_register)
					, p_src_handle_(p_handle)
					, table_index_(table_index)
				{
				}
				int max_register_;
				const D3D12_CPU_DESCRIPTOR_HANDLE* p_src_handle_;
				int table_index_;
			};
			const DescriptorSetInfo sampler_set_info[] =
			{
				DescriptorSetInfo(p_desc_set->GetVsSampler().max_use_register_index, p_desc_set->GetVsSampler().cpu_handles, resource_table.vs_sampler_table),
				DescriptorSetInfo(p_desc_set->GetPsSampler().max_use_register_index, p_desc_set->GetPsSampler().cpu_handles, resource_table.ps_sampler_table),
				DescriptorSetInfo(p_desc_set->GetGsSampler().max_use_register_index, p_desc_set->GetGsSampler().cpu_handles, resource_table.gs_sampler_table),
				DescriptorSetInfo(p_desc_set->GetHsSampler().max_use_register_index, p_desc_set->GetHsSampler().cpu_handles, resource_table.hs_sampler_table),
				DescriptorSetInfo(p_desc_set->GetDsSampler().max_use_register_index, p_desc_set->GetDsSampler().cpu_handles, resource_table.ds_sampler_table),
			};

			// Sampler用のFrameDescriptorHeapに必要分確保するため総数計算.
			auto total_samp_count = 0;
			for (const auto& e : sampler_set_info)
				total_samp_count += e.max_register_ + 1;

			// Sampler用のFrameDescriptor確保. ここでPageが足りなければ新規Pageが確保されてHeapが切り替わるので, SetDescriptorHeaps() の前に実行する必要がある.
			const auto sampler_desc_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_sampler_handle_start;
			D3D12_GPU_DESCRIPTOR_HANDLE gpu_sampler_handle_start;
			// Heap確保. 現在のPageで必要分確保できなければ新規Page(Heap)に自動で切り替わる.
			frame_desc_page_interface_for_sampler_.Allocate(total_samp_count, cpu_sampler_handle_start, gpu_sampler_handle_start);
			const u64 sampler_handle_increment_size = frame_desc_page_interface_for_sampler_.GetPool()->GetHandleIncrementSize(sampler_desc_heap_type);


			// DescriptorHeapの設定.
			// Cbv Srv Uav用とSampler用.
			// これ以前にFrameDescriptorから確保して必要ならばHeap切り替えが完了した後にCommandListにHeapを設定する.
			// CommandListにHeapを設定した後にそのHeap上のDescriptorをDescriptorTableに設定する必要がある(設定されているHeapと異なるHeap上のDescriptorをセットするとD3Dエラーとなる.)
			// CbvSrvUavのHeapは巨大な単一Heap上で確保するためアプリケーション実行中に変化しないのでSamplerとは異なりいつ設定しても良い.
			ID3D12DescriptorHeap* heaps[] =
			{
				frame_desc_interface_.GetManager()->GetD3D12DescriptorHeap(),
				frame_desc_page_interface_for_sampler_.GetD3D12DescriptorHeap()
			};
			p_command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);


			// Samplerのコミット.
			{
				// 各ステージのSamplerを設定.
				D3D12_CPU_DESCRIPTOR_HANDLE tmp[k_sampler_table_size];
				auto SetSamplerDescriptor = [&](
					D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
					D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu_handle_start,
					D3D12_GPU_DESCRIPTOR_HANDLE dst_gpu_handle_start,
					u32 src_count, const D3D12_CPU_DESCRIPTOR_HANDLE* src_handle,
					u8 table_index)
				{
					if (0 > table_index || 0 >= src_count)
						return;

					for (u32 i = 0; i < src_count; i++)
					{
						tmp[i] = (src_handle[i].ptr > 0) ? src_handle[i] : def_descriptor.cpu_handle;
					}

					// FrameDescriptorHeapから連続したDescriptorを確保してコピー,CommandListへセットする.
					parent_device_->GetD3D12Device()->CopyDescriptors(
						1, &dst_cpu_handle_start, &src_count,
						src_count, tmp, nullptr,
						heap_type);
					p_command_list_->SetGraphicsRootDescriptorTable(table_index, dst_gpu_handle_start);
				};

				{
					for (const auto& e : sampler_set_info)
					{
						const auto copy_count = e.max_register_ + 1;
						// 指定のFrameDescriptor開始位置から始まる範囲にDescriptorをコピーしてCommandListに設定.
						SetSamplerDescriptor(sampler_desc_heap_type, cpu_sampler_handle_start, gpu_sampler_handle_start, copy_count, e.p_src_handle_, e.table_index_);

						// FrameDescriptor上のポインタを進行.
						const auto offset_size = sampler_handle_increment_size * static_cast<u64>(copy_count);
						cpu_sampler_handle_start.ptr += offset_size;
						gpu_sampler_handle_start.ptr += offset_size;
					}
				}
			}

			// CBV, SRV, UAVのコミット.
			{
				const auto cvbsrvuav_desc_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

				D3D12_CPU_DESCRIPTOR_HANDLE tmp[k_srv_table_size];
				auto SetViewDescriptor = [&](u32 count, const D3D12_CPU_DESCRIPTOR_HANDLE* handles, u8 table_index)
				{
					if (0 > table_index || 0 >= count)
						return;

					for (u32 i = 0; i < count; i++)
					{
						tmp[i] = (handles[i].ptr > 0) ? handles[i] : def_descriptor.cpu_handle;
					}

					D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu;
					D3D12_GPU_DESCRIPTOR_HANDLE dst_gpu;
					frame_desc_interface_.Allocate(count, dst_cpu, dst_gpu);

					// FrameDescriptorHeapから連続したDescriptorを確保してコピー,CommandListへセットする.
					parent_device_->GetD3D12Device()->CopyDescriptors(
						1, &dst_cpu, &count,
						count, tmp, nullptr,
						cvbsrvuav_desc_heap_type);
					p_command_list_->SetGraphicsRootDescriptorTable(table_index, dst_gpu);
				};
				// 各ステージの各リソースタイプ別に連続Descriptorを確保,コピーしてテーブルにをセットしていく
				// 各ステージ毎各リソースタイプ毎に0番から設定された最大レジスタ番号までの範囲でFrameDescriptorから確保してコピー,CommandListへ設定する.
				SetViewDescriptor(p_desc_set->GetVsCbv().max_use_register_index + 1, p_desc_set->GetVsCbv().cpu_handles, resource_table.vs_cbv_table);
				SetViewDescriptor(p_desc_set->GetVsSrv().max_use_register_index + 1, p_desc_set->GetVsSrv().cpu_handles, resource_table.vs_srv_table);

				// 現状はUAVはPSのみ.(CSは別関数)
				SetViewDescriptor(p_desc_set->GetPsCbv().max_use_register_index + 1, p_desc_set->GetPsCbv().cpu_handles, resource_table.ps_cbv_table);
				SetViewDescriptor(p_desc_set->GetPsSrv().max_use_register_index + 1, p_desc_set->GetPsSrv().cpu_handles, resource_table.ps_srv_table);
				SetViewDescriptor(p_desc_set->GetPsUav().max_use_register_index + 1, p_desc_set->GetPsUav().cpu_handles, resource_table.ps_uav_table);

				SetViewDescriptor(p_desc_set->GetGsCbv().max_use_register_index + 1, p_desc_set->GetGsCbv().cpu_handles, resource_table.gs_cbv_table);
				SetViewDescriptor(p_desc_set->GetGsSrv().max_use_register_index + 1, p_desc_set->GetGsSrv().cpu_handles, resource_table.gs_srv_table);

				SetViewDescriptor(p_desc_set->GetHsCbv().max_use_register_index + 1, p_desc_set->GetHsCbv().cpu_handles, resource_table.hs_cbv_table);
				SetViewDescriptor(p_desc_set->GetHsSrv().max_use_register_index + 1, p_desc_set->GetHsSrv().cpu_handles, resource_table.hs_srv_table);

				SetViewDescriptor(p_desc_set->GetDsCbv().max_use_register_index + 1, p_desc_set->GetDsCbv().cpu_handles, resource_table.ds_cbv_table);
				SetViewDescriptor(p_desc_set->GetDsSrv().max_use_register_index + 1, p_desc_set->GetDsSrv().cpu_handles, resource_table.ds_srv_table);
			}
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		ScopedEventMarker::ScopedEventMarker(CommandListBaseDep* p_command_list, const char* label)
			:p_command_list_(p_command_list)
		{
			if(p_command_list_)
			{	
				p_command_list_->BeginMarker(label);
			}
		}
		ScopedEventMarker::~ScopedEventMarker()
		{
			if(p_command_list_)
			{
				p_command_list_->EndMarker();
			}
			p_command_list_ = {};
		}

	}
}
#pragma once

#include <iostream>
#include <vector>

#include "util/types.h"

#include "rhi/rhi.h"
#include "rhi/rhi_object_garbage_collect.h"

#include "rhi/d3d12/rhi_util.d3d12.h"
#include "descriptor.d3d12.h"

// 必要であればバージョン制限しておく(DXRのための4等).
//#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 4

// Enhanced Barrier バッチ発行モード切り替えマクロ.
// 1: バッチモード(Draw/Dispatch/Clear系の直前にまとめて発行), 0: 即時発行(従来動作).
#	define NGL_ENHANCED_BARRIER_BATCH 1

// CommandList ターゲットバージョン コンパイル時定数.
// このマクロをインクルード前に定義することで使用するターゲットバージョンを明示的に指定可能.
// 未定義の場合は現在のSDKで利用可能な最大バージョンを自動選択する.
#if !defined(NGL_D3D12_COMMAND_LIST_TARGET_VERSION)
#	if defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 10
#	elif defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 9
#	elif defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 8
#	elif defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 7
#	elif defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 6
#	elif defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 5
#	elif defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 4
#	elif defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 3
#	elif defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 2
#	elif defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 1
#	else
#		define NGL_D3D12_COMMAND_LIST_TARGET_VERSION 0
#	endif
#endif

// Enhanced Barrier バッチ発行モード切り替えマクロ.
// 1: バッチモード(Draw/Dispatch/Clear系の直前にまとめて発行), 0: 即時発行(従来動作).
// コンパイル時に 0/1 を切り替えてパフォーマンス比較が可能.
#if !defined(NGL_ENHANCED_BARRIER_BATCH)
#	define NGL_ENHANCED_BARRIER_BATCH 1
#endif

namespace ngl
{
	namespace rhi
	{
		// NGL_D3D12_COMMAND_LIST_TARGET_VERSION に対応するCommandList Interface型エイリアス.
		// ビルド時にターゲットとして指定されたバージョンのID3D12GraphicsCommandListN 型.
#if NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 10 && defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList10;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 9 && defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList9;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 8 && defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList8;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 7 && defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList7;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 6 && defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList6;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 5 && defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList5;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 4 && defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList4;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 3 && defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList3;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 2 && defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList2;
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 1 && defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList1;
#else
		using D3D12GraphicsCommandListTargetVersion = ID3D12GraphicsCommandList;
#endif

		class Device;
		class SwapChainDep;

		class TextureDep;
		class BufferDep;
		class RenderTargetViewDep;
		class DepthStencilViewDep;

		class GraphicsPipelineStateDep;
		class ComputePipelineStateDep;


		class CommandListBaseDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			};
		public:
			CommandListBaseDep() = default;
			virtual  ~CommandListBaseDep() = default;

			bool Initialize(DeviceDep* p_device, const Desc& desc);

		public:
			void Begin();
			void End();

			// Begin()状態なら true.
			bool IsOpen() const {return is_open_;}
			
			// Graphics/Compute共通のCompute用PSO設定実装.
			void SetPipelineState(ComputePipelineStateDep* p_pso);
			// Graphics/Compute共通のCompute用DescriptorSet設定実装.
			void SetDescriptorSet(const ComputePipelineStateDep* p_pso, const DescriptorSetDep* p_desc_set);
			
			void Dispatch(u32 x, u32 y, u32 z);
			void DispatchIndirect(BufferDep* p_arg_buffer);

#if defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
			// DXR: RaytracingAccelerationStructure のビルドコマンドを発行.
			// ペンディングバリアを内部で自動フラッシュしてから実行する.
			void BuildRaytracingAccelerationStructure(
				const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* p_desc,
				UINT num_postbuild_info_descs = 0,
				const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* p_postbuild_info_descs = nullptr);

			// DXR: DispatchRays コマンドを発行.
			// ペンディングバリアを内部で自動フラッシュしてから実行する.
			void DispatchRays(const D3D12_DISPATCH_RAYS_DESC* p_desc);
#endif // __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__

			// Buffer 全体を別の Buffer へコピー.
			// ペンディングバリアを内部で自動フラッシュしてから実行する.
			void CopyResource(const BufferDep* p_dst, const BufferDep* p_src);

			// UAV同期Barrier.
			void ResourceUavBarrier(TextureDep* p_texture);
			// UAV同期Barrier.
			void ResourceUavBarrier(BufferDep* p_buffer);

		public:
			// Gpu Event Marker. マクロ NGL_SCOPED_EVENT_MARKER で利用される.
			void BeginMarker(const char* format, ...);
			// Gpu Event Marker. マクロ NGL_SCOPED_EVENT_MARKER で利用される.
			void EndMarker();
			
		public:
			FrameCommandListDynamicDescriptorAllocatorInterface* GetFrameDescriptorInterface() { return &frame_desc_interface_; }
			FrameDescriptorHeapPageInterface* GetFrameSamplerDescriptorHeapInterface() { return &frame_desc_page_interface_for_sampler_; }

			DeviceDep* GetDevice() { return parent_device_; }
			const Desc& GetDesc() const {return desc_;}
			
		public:
			// CommandListの標準Interfaceを取得.
			ID3D12GraphicsCommandList* GetD3D12GraphicsCommandList()
			{
				return p_command_list_.Get();
			}

#if defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList1* GetD3D12GraphicsCommandList1()
			{
				return p_command_list1_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList2* GetD3D12GraphicsCommandList2()
			{
				return p_command_list2_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList3* GetD3D12GraphicsCommandList3()
			{
				return p_command_list3_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList4* GetD3D12GraphicsCommandList4()
			{
				return p_command_list4_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList5* GetD3D12GraphicsCommandList5()
			{
				return p_command_list5_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList6* GetD3D12GraphicsCommandList6()
			{
				return p_command_list6_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList7* GetD3D12GraphicsCommandList7()
			{
				return p_command_list7_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList8* GetD3D12GraphicsCommandList8()
			{
				return p_command_list8_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList9* GetD3D12GraphicsCommandList9()
			{
				return p_command_list9_.Get();
			}
#endif
#if defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
			ID3D12GraphicsCommandList10* GetD3D12GraphicsCommandList10()
			{
				return p_command_list10_.Get();
			}
#endif

			// NGL_D3D12_COMMAND_LIST_TARGET_VERSION で指定したターゲットバージョンのCommandListを取得.
			// 戻り値の型は D3D12GraphicsCommandListTargetVersion (コンパイル時定数で決まる ID3D12GraphicsCommandListN).
			D3D12GraphicsCommandListTargetVersion* GetD3D12GraphicsCommandListTargetVersion()
			{
#if NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 10 && defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
				return p_command_list10_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 9 && defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
				return p_command_list9_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 8 && defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
				return p_command_list8_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 7 && defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
				return p_command_list7_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 6 && defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
				return p_command_list6_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 5 && defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
				return p_command_list5_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 4 && defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
				return p_command_list4_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 3 && defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
				return p_command_list3_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 2 && defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
				return p_command_list2_.Get();
#elif NGL_D3D12_COMMAND_LIST_TARGET_VERSION >= 1 && defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
				return p_command_list1_.Get();
#else
				return p_command_list_.Get();
#endif
			}

			ID3D12GraphicsCommandList* GetD3D12GraphicsCommandListLatest()
			{
#if defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
				if (p_command_list10_)
					return p_command_list10_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
				if (p_command_list9_)
					return p_command_list9_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
				if (p_command_list8_)
					return p_command_list8_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
				if (p_command_list7_)
					return p_command_list7_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
				if (p_command_list6_)
					return p_command_list6_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
				if (p_command_list5_)
					return p_command_list5_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
				if (p_command_list4_)
					return p_command_list4_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
				if (p_command_list3_)
					return p_command_list3_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
				if (p_command_list2_)
					return p_command_list2_.Get();
#endif
#if defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
				if (p_command_list1_)
					return p_command_list1_.Get();
#endif
				return p_command_list_.Get();
			}
			
		protected:
			DeviceDep* parent_device_	= nullptr;
			Desc		desc_ = {};
			bool		is_open_ = false;// Begin,Closeの状態を保持.

			// Cvb Srv Uav用.
			FrameCommandListDynamicDescriptorAllocatorInterface	frame_desc_interface_ = {};
			// Sampler用.
			FrameDescriptorHeapPageInterface	frame_desc_page_interface_for_sampler_ = {};

			Microsoft::WRL::ComPtr<ID3D12CommandAllocator>		p_command_allocator_;

			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>	p_command_list_;

#if defined(__ID3D12GraphicsCommandList1_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList1>	p_command_list1_;
#endif
#if defined(__ID3D12GraphicsCommandList2_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2>	p_command_list2_;
#endif
#if defined(__ID3D12GraphicsCommandList3_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList3>	p_command_list3_;
#endif
#if defined(__ID3D12GraphicsCommandList4_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>	p_command_list4_;
#endif
#if defined(__ID3D12GraphicsCommandList5_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5>	p_command_list5_;
#endif
#if defined(__ID3D12GraphicsCommandList6_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6>	p_command_list6_;
#endif
#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>	p_command_list7_;
#endif
#if defined(__ID3D12GraphicsCommandList8_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList8>	p_command_list8_;
#endif
#if defined(__ID3D12GraphicsCommandList9_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList9>	p_command_list9_;
#endif
#if defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__)
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList10>	p_command_list10_;
#endif

			// CommandSignature for DispatchIndirect
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> p_dispatch_indirect_command_signature_;

#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
#if NGL_ENHANCED_BARRIER_BATCH
			// Enhanced Barrier バッチ発行用ペンディングリスト.
			std::vector<D3D12_TEXTURE_BARRIER> pending_tex_barriers_;
			std::vector<D3D12_BUFFER_BARRIER>  pending_buf_barriers_;
#endif // NGL_ENHANCED_BARRIER_BATCH
#endif // __ID3D12GraphicsCommandList7_INTERFACE_DEFINED__

		public:
			// ペンディングバリアを一括発行. Draw/Dispatch/Clear系の直前に内部で自動呼び出しされる.
			// 生の D3D12 インターフェース経由で GPU 実行命令を発行する場合は事前に明示的に呼び出すこと.
			// NGL_ENHANCED_BARRIER_BATCH == 0 の場合は no-op.
			void FlushPendingBarriers();
		};

		
		// Compute CommandList. for AsyncCompute.
		// Async Compute のComputeQueueで使用可能な機能のみ公開している.
		class ComputeCommandListDep : public CommandListBaseDep
		{
		public:
			ComputeCommandListDep();
			~ComputeCommandListDep();

			bool Initialize(DeviceDep* p_device);
			void Finalize();

		public:
			// 使用可能な機能はBほとんどBaseで実装.
		};
		
		// Graphics CommandList.
		class GraphicsCommandListDep : public CommandListBaseDep
		{
		public:
			GraphicsCommandListDep();
			~GraphicsCommandListDep();

			bool Initialize(DeviceDep* p_device);
			void Finalize();

		public:
			void SetRenderTargets(const RenderTargetViewDep** pp_rtv, int num_rtv, const DepthStencilViewDep* p_dsv);

			void SetViewports(u32 num, const  D3D12_VIEWPORT* p_viewports);
			void SetScissor(u32 num, const  D3D12_RECT* p_rects);

			using CommandListBaseDep::SetPipelineState;
			void SetPipelineState(GraphicsPipelineStateDep* p_pso);
			using CommandListBaseDep::SetDescriptorSet;
			void SetDescriptorSet(const GraphicsPipelineStateDep* p_pso, const DescriptorSetDep* p_desc_set);


			void SetPrimitiveTopology(EPrimitiveTopology topology);
			void SetVertexBuffers(u32 slot, u32 num, const D3D12_VERTEX_BUFFER_VIEW* p_views);
			void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* p_view);

			void DrawInstanced(u32 num_vtx, u32 num_instance, u32 offset_vtx, u32 offset_instance);
			void DrawIndexedInstanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, s32  base_vertex_location, u32 start_instance_location);
			void DrawIndirect(BufferDep* p_arg_buffer);


			void ClearRenderTarget(const RenderTargetViewDep* p_rtv, const float(color)[4]);
			void ClearDepthTarget(const DepthStencilViewDep* p_dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil);

			// Barrier Swapchain, Texture, Buffer.
			void ResourceBarrier(SwapChainDep* p_swapchain, unsigned int buffer_index, EResourceState prev, EResourceState next);
			void ResourceBarrier(TextureDep* p_texture, EResourceState prev, EResourceState next);
			void ResourceBarrier(BufferDep* p_buffer, EResourceState prev, EResourceState next);

		private:
			// CommandSignature for DrawIndirect
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> p_draw_indirect_command_signature_;
		};

		// Gpu Event Marker用.
		//	マクロ NGL_SCOPED_EVENT_MARKER での利用推奨.
		struct ScopedEventMarker
		{
			ScopedEventMarker(CommandListBaseDep* p_command_list, const char* label);
			~ScopedEventMarker();

		private:
			CommandListBaseDep* p_command_list{};
		};
	}
}

// https://www.jpcert.or.jp/sc-rules/c-pre05-c.html
#define NGL_RHI_JOIN_AGAIN_NGL_GPU_SCOPED_EVENT_MARKER(a,b) a ## b
#define NGL_RHI_JOIN_NGL_GPU_SCOPED_EVENT_MARKER(a,b) NGL_RHI_JOIN_AGAIN_NGL_GPU_SCOPED_EVENT_MARKER(a, b)
// GPU Scoped Event Marker 定義用マクロ.
//	ex. NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, "BasePass");
#define NGL_RHI_GPU_SCOPED_EVENT_MARKER(p_command_list, label) const ngl::rhi::ScopedEventMarker NGL_RHI_JOIN_NGL_GPU_SCOPED_EVENT_MARKER(scoped_event_arg_ , __LINE__) (p_command_list, label);


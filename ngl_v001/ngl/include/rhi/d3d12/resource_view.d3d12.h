#pragma once

#include <iostream>
#include <memory>

#include "util/types.h"
#include "text/hash_text.h"


#include "rhi/rhi.h"
#include "rhi/rhi_object_garbage_collect.h"
#include "descriptor.d3d12.h"



namespace ngl
{
	namespace rhi
	{
		class DeviceDep;
		class SwapChainDep;

		class BufferDep;
		class TextureDep;


		using RefVbvDep = RhiRef<class VertexBufferViewDep>;
		using RefIbvDep = RhiRef<class IndexBufferViewDep>;

		using RefSampDep = RhiRef<class SamplerDep>;

		using RefCbvDep = RhiRef<class ConstantBufferViewDep>;
		using RefSrvDep = RhiRef<class ShaderResourceViewDep>;
		using RefUavDep = RhiRef<class UnorderedAccessViewDep>;
		using RefRtvDep = RhiRef<class RenderTargetViewDep>;
		using RefDsvDep = RhiRef<class DepthStencilViewDep>;



		// VertexBufferView
		class VertexBufferViewDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				ngl::u32			dummy;
			};

			VertexBufferViewDep();
			~VertexBufferViewDep();

			bool Initialize(BufferDep* p_buffer, const Desc& desc);
			void Finalize();

			const D3D12_VERTEX_BUFFER_VIEW& GetView() const
			{
				return view_;
			}

		private:
			D3D12_VERTEX_BUFFER_VIEW	view_ = {};
		};

		// IndexBufferView
		class IndexBufferViewDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				ngl::u32			dummy;
			};

			IndexBufferViewDep();
			~IndexBufferViewDep();

			bool Initialize(BufferDep* p_buffer, const Desc& desc);
			void Finalize();

			const D3D12_INDEX_BUFFER_VIEW& GetView() const
			{
				return view_;
			}

		private:
			D3D12_INDEX_BUFFER_VIEW	view_ = {};
		};

		// ConstantBufferView
		class ConstantBufferViewDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				ngl::u32			dummy;
			};

			ConstantBufferViewDep();
			~ConstantBufferViewDep();

			bool Initialize(BufferDep* p_buffer, const Desc& desc);
			void Finalize();

			const PersistentDescriptorInfo& GetView() const
			{
				return view_;
			}

		private:
			PersistentDescriptorInfo	view_ = {};
		};

		// SamplerState
		class SamplerDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				ETextureFilterMode	Filter		= ETextureFilterMode::Min_Linear_Mag_Linear_Mip_Linear;
				ETextureAddressMode	AddressU	= ETextureAddressMode::Repeat;
				ETextureAddressMode	AddressV	= ETextureAddressMode::Repeat;
				ETextureAddressMode	AddressW	= ETextureAddressMode::Repeat;
				float				MipLODBias	= 0.0;
				u32					MaxAnisotropy	= 0;
				ECompFunc			ComparisonFunc	= ECompFunc::Never;
				float				BorderColor[4]	= { 0.0f,0.0f,0.0f,0.0f };
				float				MinLOD			= 0.0f;
				float				MaxLOD			= 128.0f;
			};
			SamplerDep();
			~SamplerDep();
			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			const PersistentDescriptorInfo& GetView() const
			{
				return view_;
			}
		private:
			PersistentDescriptorInfo	view_{};
		};



		// RenderTargetView
		class RenderTargetViewDep : public RhiObjectBase
		{
		public:
			RenderTargetViewDep();
			~RenderTargetViewDep();

			// SwapChainから作成.
			bool Initialize(DeviceDep* p_device, SwapChainDep* p_swapchain, unsigned int buffer_index);

			// Textureから生成.
			bool Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size);

			void Finalize();

			D3D12_CPU_DESCRIPTOR_HANDLE GetD3D12DescriptorHandle() const
			{
				return p_heap_->GetCPUDescriptorHandleForHeapStart();
			}
		private:
			// 現状のRTVやDSVはHeapをそれぞれ専有する. 問題があればグローバルな専用Heapから確保する.
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> p_heap_;
		};

		// DepthStencilView
		class DepthStencilViewDep : public RhiObjectBase
		{
		public:
			DepthStencilViewDep();
			~DepthStencilViewDep();

			// Textureから生成.
			bool Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size);

			void Finalize();

			D3D12_CPU_DESCRIPTOR_HANDLE GetD3D12DescriptorHandle() const
			{
				return p_heap_->GetCPUDescriptorHandleForHeapStart();
			}
		private:
			// 現状のRTVやDSVはHeapをそれぞれ専有する. 問題があればグローバルな専用Heapから確保する.
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> p_heap_;
		};

		// UnorderedAccessViewDep
		class UnorderedAccessViewDep : public RhiObjectBase
		{
		public:
			UnorderedAccessViewDep();
			~UnorderedAccessViewDep();

			// TextureのView.
			bool Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size);
			// BufferのStructuredBufferView.
			bool InitializeAsStructured(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_size, u32 element_offset, u32 element_count);
			// BufferのTypedBufferView.
			bool InitializeAsTyped(DeviceDep* p_device, const BufferDep* p_buffer, EResourceFormat format, u32 element_offset, u32 element_count);
			// BufferのByteAddressBufferView.
			bool InitializeAsRaw(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_offset, u32 element_count);

			void Finalize();

			const PersistentDescriptorInfo& GetView() const
			{
				return view_;
			}
		private:
			PersistentDescriptorInfo	view_ = {};
		};

		// ShaderResourceView
		class ShaderResourceViewDep : public RhiObjectBase
		{
		public:
			ShaderResourceViewDep();
			~ShaderResourceViewDep();

			// TextureのView.
			bool InitializeAsTexture(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 mip_count, u32 first_array_slice, u32 array_size);
			// BufferのStructuredBufferView.
			bool InitializeAsStructured(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_size, u32 element_offset, u32 element_count);
			// BufferのTypedBufferView.
			bool InitializeAsTyped(DeviceDep* p_device, const BufferDep* p_buffer, EResourceFormat format, u32 element_offset, u32 element_count);
			// BufferのByteAddressBufferView.
			bool InitializeAsRaw(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_offset, u32 element_count);
			// BufferのRaytracingAccelerationStructureView.
			bool InitializeAsRaytracingAccelerationStructure(DeviceDep* p_device, const BufferDep* p_buffer);

			void Finalize();

			const PersistentDescriptorInfo& GetView() const
			{
				return view_;
			}
		private:
			PersistentDescriptorInfo	view_ = {};
		};

	}
}
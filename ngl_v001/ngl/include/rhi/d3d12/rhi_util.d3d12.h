#pragma once

#include "rhi/rhi.h"


#include <iostream>
#include <vector>
#include <unordered_map>


#include "util/types.h"
#include "text/hash_text.h"

#include <d3d12.h>
#if 1
#include <dxgi1_6.h>
#else
#include <dxgi1_4.h>
#endif

//#include <atlbase.h>
#include <wrl/client.h>

namespace ngl
{
	namespace rhi
	{


		DXGI_FORMAT ConvertResourceFormat(EResourceFormat v);
		// 逆変換. 内部で探索が実行されるため高速ではない.
		EResourceFormat ConvertResourceFormat(DXGI_FORMAT v);

		D3D12_RESOURCE_STATES ConvertResourceState(EResourceState v);

		D3D12_BLEND_OP	ConvertBlendOp(EBlendOp v);

		D3D12_BLEND	ConvertBlendFactor(EBlendFactor v);

		D3D12_CULL_MODE	ConvertCullMode(ECullingMode v);

		D3D12_FILL_MODE ConvertFillMode(EFillMode v);

		D3D12_STENCIL_OP ConvertStencilOp(EStencilOp v);

		D3D12_COMPARISON_FUNC ConvertComparisonFunc(ECompFunc v);

		D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertPrimitiveTopologyType(EPrimitiveTopologyType v);

		D3D_PRIMITIVE_TOPOLOGY ConvertPrimitiveTopology(EPrimitiveTopology v);

		D3D12_FILTER ConvertTextureFilter(ETextureFilterMode v);

		D3D12_TEXTURE_ADDRESS_MODE ConvertTextureAddressMode(ETextureAddressMode v);


		// Enhanced Barrier 状態情報. EResourceState から変換して使用する.
		// Textureの場合のみ layout フィールドが意味を持つ. BufferバリアのSyncとAccessは同じ構造体を共用.
#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
		struct EnhancedBarrierStateInfo
		{
			D3D12_BARRIER_SYNC   sync   = D3D12_BARRIER_SYNC_NONE;
			D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;
			D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_COMMON; // Texture用のみ利用
		};
		EnhancedBarrierStateInfo ConvertResourceStateToEnhancedBarrierInfo(EResourceState v);
#endif



		constexpr uint32_t align_to(uint32_t alignment, uint32_t value)
		{
			return (((value + alignment - 1) / alignment) * alignment);
		}

	}
}
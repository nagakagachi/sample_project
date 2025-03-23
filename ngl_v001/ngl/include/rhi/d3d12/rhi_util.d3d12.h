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



		constexpr uint32_t align_to(uint32_t alignment, uint32_t value)
		{
			return (((value + alignment - 1) / alignment) * alignment);
		}

	}
}
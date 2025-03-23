

#include "rhi/d3d12/rhi_util.d3d12.h"

namespace ngl
{
	namespace rhi
	{
		DXGI_FORMAT ConvertResourceFormat(EResourceFormat v)
		{
			static constexpr DXGI_FORMAT table[] = 
			{
				DXGI_FORMAT_UNKNOWN,
				DXGI_FORMAT_R32G32B32A32_TYPELESS,
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				DXGI_FORMAT_R32G32B32A32_UINT,
				DXGI_FORMAT_R32G32B32A32_SINT,
				DXGI_FORMAT_R32G32B32_TYPELESS,
				DXGI_FORMAT_R32G32B32_FLOAT,
				DXGI_FORMAT_R32G32B32_UINT,
				DXGI_FORMAT_R32G32B32_SINT,
				DXGI_FORMAT_R16G16B16A16_TYPELESS,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R16G16B16A16_UNORM,
				DXGI_FORMAT_R16G16B16A16_UINT,
				DXGI_FORMAT_R16G16B16A16_SNORM,
				DXGI_FORMAT_R16G16B16A16_SINT,
				DXGI_FORMAT_R32G32_TYPELESS,
				DXGI_FORMAT_R32G32_FLOAT,
				DXGI_FORMAT_R32G32_UINT,
				DXGI_FORMAT_R32G32_SINT,
				DXGI_FORMAT_R32G8X24_TYPELESS,
				DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
				DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
				DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
				DXGI_FORMAT_R10G10B10A2_TYPELESS,
				DXGI_FORMAT_R10G10B10A2_UNORM,
				DXGI_FORMAT_R10G10B10A2_UINT,
				DXGI_FORMAT_R11G11B10_FLOAT,
				DXGI_FORMAT_R8G8B8A8_TYPELESS,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				DXGI_FORMAT_R8G8B8A8_UINT,
				DXGI_FORMAT_R8G8B8A8_SNORM,
				DXGI_FORMAT_R8G8B8A8_SINT,
				DXGI_FORMAT_R16G16_TYPELESS,
				DXGI_FORMAT_R16G16_FLOAT,
				DXGI_FORMAT_R16G16_UNORM,
				DXGI_FORMAT_R16G16_UINT,
				DXGI_FORMAT_R16G16_SNORM,
				DXGI_FORMAT_R16G16_SINT,
				DXGI_FORMAT_R32_TYPELESS,
				DXGI_FORMAT_D32_FLOAT,
				DXGI_FORMAT_R32_FLOAT,
				DXGI_FORMAT_R32_UINT,
				DXGI_FORMAT_R32_SINT,
				DXGI_FORMAT_R24G8_TYPELESS,
				DXGI_FORMAT_D24_UNORM_S8_UINT,
				DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
				DXGI_FORMAT_X24_TYPELESS_G8_UINT,
				DXGI_FORMAT_R8G8_TYPELESS,
				DXGI_FORMAT_R8G8_UNORM,
				DXGI_FORMAT_R8G8_UINT,
				DXGI_FORMAT_R8G8_SNORM,
				DXGI_FORMAT_R8G8_SINT,
				DXGI_FORMAT_R16_TYPELESS,
				DXGI_FORMAT_R16_FLOAT,
				DXGI_FORMAT_D16_UNORM,
				DXGI_FORMAT_R16_UNORM,
				DXGI_FORMAT_R16_UINT,
				DXGI_FORMAT_R16_SNORM,
				DXGI_FORMAT_R16_SINT,
				DXGI_FORMAT_R8_TYPELESS,
				DXGI_FORMAT_R8_UNORM,
				DXGI_FORMAT_R8_UINT,
				DXGI_FORMAT_R8_SNORM,
				DXGI_FORMAT_R8_SINT,
				DXGI_FORMAT_A8_UNORM,
				DXGI_FORMAT_R1_UNORM,
				DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
				DXGI_FORMAT_R8G8_B8G8_UNORM,
				DXGI_FORMAT_G8R8_G8B8_UNORM,
				DXGI_FORMAT_BC1_TYPELESS,
				DXGI_FORMAT_BC1_UNORM,
				DXGI_FORMAT_BC1_UNORM_SRGB,
				DXGI_FORMAT_BC2_TYPELESS,
				DXGI_FORMAT_BC2_UNORM,
				DXGI_FORMAT_BC2_UNORM_SRGB,
				DXGI_FORMAT_BC3_TYPELESS,
				DXGI_FORMAT_BC3_UNORM,
				DXGI_FORMAT_BC3_UNORM_SRGB,
				DXGI_FORMAT_BC4_TYPELESS,
				DXGI_FORMAT_BC4_UNORM,
				DXGI_FORMAT_BC4_SNORM,
				DXGI_FORMAT_BC5_TYPELESS,
				DXGI_FORMAT_BC5_UNORM,
				DXGI_FORMAT_BC5_SNORM,
				DXGI_FORMAT_B5G6R5_UNORM,
				DXGI_FORMAT_B5G5R5A1_UNORM,
				DXGI_FORMAT_B8G8R8A8_UNORM,
				DXGI_FORMAT_B8G8R8X8_UNORM,
				DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
				DXGI_FORMAT_B8G8R8A8_TYPELESS,
				DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
				DXGI_FORMAT_B8G8R8X8_TYPELESS,
				DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
				DXGI_FORMAT_BC6H_TYPELESS,
				DXGI_FORMAT_BC6H_UF16,
				DXGI_FORMAT_BC6H_SF16,
				DXGI_FORMAT_BC7_TYPELESS,
				DXGI_FORMAT_BC7_UNORM,
				DXGI_FORMAT_BC7_UNORM_SRGB,
				DXGI_FORMAT_AYUV,
				DXGI_FORMAT_Y410,
				DXGI_FORMAT_Y416,
				DXGI_FORMAT_NV12,
				DXGI_FORMAT_P010,
				DXGI_FORMAT_P016,
				DXGI_FORMAT_420_OPAQUE,
				DXGI_FORMAT_YUY2,
				DXGI_FORMAT_Y210,
				DXGI_FORMAT_Y216,
				DXGI_FORMAT_NV11,
				DXGI_FORMAT_AI44,
				DXGI_FORMAT_IA44,
				DXGI_FORMAT_P8,
				DXGI_FORMAT_A8P8,
				DXGI_FORMAT_B4G4R4A4_UNORM
			};
			static_assert(std::size(table) == static_cast<size_t>(EResourceFormat::_MAX), "");
			
			// 現状はDXGIと一対一の対応
			return table[static_cast<u32>(v)];
		}
		// DirectX->Ngl Convert Format. 探索が発生するため高速ではない.
		EResourceFormat ConvertResourceFormat(DXGI_FORMAT v)
		{
			for(int i = 0; i < static_cast<int>(EResourceFormat::_MAX); ++i)
			{
				if(v == ConvertResourceFormat(static_cast<EResourceFormat>(i)))
					return static_cast<EResourceFormat>(i);
			}
			return EResourceFormat::Format_UNKNOWN;
		}

		D3D12_RESOURCE_STATES ConvertResourceState(EResourceState v)
		{
			D3D12_RESOURCE_STATES ret = {};
			switch (v)
			{
			case EResourceState::Common:
			{
				ret = D3D12_RESOURCE_STATE_COMMON;
				break;
			}
			case EResourceState::General:
			{
				ret = D3D12_RESOURCE_STATE_GENERIC_READ;
				break;
			}
			case EResourceState::ConstatnBuffer:
			{
				ret = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
				break;
			}
			case EResourceState::VertexBuffer:
			{
				ret = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
				break;
			}
			case EResourceState::IndexBuffer:
			{
				ret = D3D12_RESOURCE_STATE_INDEX_BUFFER;
				break;
			}
			case EResourceState::RenderTarget:
			{
				ret = D3D12_RESOURCE_STATE_RENDER_TARGET;
				break;
			}
			case EResourceState::ShaderRead:
			{
				ret = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				break;
			}
			case EResourceState::UnorderedAccess:
			{
				ret = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				break;
			}
			case EResourceState::DepthWrite:
			{
				ret = D3D12_RESOURCE_STATE_DEPTH_WRITE;
				break;
			}
			case EResourceState::DepthRead:
			{
				ret = D3D12_RESOURCE_STATE_DEPTH_READ;
				break;
			}
			case EResourceState::IndirectArgument:
			{
				ret = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
				break;
			}
			case EResourceState::CopyDst:
			{
				ret = D3D12_RESOURCE_STATE_COPY_DEST;
				break;
			}
			case EResourceState::CopySrc:
			{
				ret = D3D12_RESOURCE_STATE_COPY_SOURCE;
				break;
			}
			case EResourceState::RaytracingAccelerationStructure:
			{
				ret = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
				break;
			}
			case EResourceState::Present:
			{
				ret = D3D12_RESOURCE_STATE_PRESENT;
				break;
			}
			default:
			{
				std::cout << "ERROR : Invalid Resource State" << std::endl;
			}
			}
			return ret;
		}

		D3D12_BLEND_OP	ConvertBlendOp(EBlendOp v)
		{
			switch (v)
			{
			case EBlendOp::Add:
			{
				return D3D12_BLEND_OP_ADD;
			}
			case EBlendOp::Subtract:
			{
				return D3D12_BLEND_OP_SUBTRACT;
			}
			case EBlendOp::RevSubtract:
			{
				return D3D12_BLEND_OP_REV_SUBTRACT;
			}
			case EBlendOp::Min:
			{
				return D3D12_BLEND_OP_MIN;
			}
			case EBlendOp::Max:
			{
				return D3D12_BLEND_OP_MAX;
			}
			default:
			{
				return D3D12_BLEND_OP_ADD;
			}
			}
		}

		D3D12_BLEND	ConvertBlendFactor(EBlendFactor v)
		{
			switch (v)
			{
				case EBlendFactor::Zero:
				{
					return D3D12_BLEND_ZERO;
				}
				case EBlendFactor::One:
				{
					return D3D12_BLEND_ONE;
				}
				case EBlendFactor::SrcColor:
				{
					return D3D12_BLEND_SRC_COLOR;
				}
				case EBlendFactor::InvSrcColor:
				{
					return D3D12_BLEND_INV_SRC_COLOR;
				}
				case EBlendFactor::SrcAlpha:
				{
					return D3D12_BLEND_SRC_ALPHA;
				}
				case EBlendFactor::InvSrcAlpha:
				{
					return D3D12_BLEND_INV_SRC_ALPHA;
				}
				case EBlendFactor::DestAlpha:
				{
					return D3D12_BLEND_DEST_ALPHA;
				}
				case EBlendFactor::InvDestAlpha:
				{
					return D3D12_BLEND_INV_DEST_ALPHA;
				}
				case EBlendFactor::DestColor:
				{
					return D3D12_BLEND_DEST_COLOR;
				}
				case EBlendFactor::InvDestColor:
				{
					return D3D12_BLEND_INV_DEST_COLOR;
				}
				case EBlendFactor::SrcAlphaSat:
				{
					return D3D12_BLEND_SRC_ALPHA_SAT;
				}
				case EBlendFactor::BlendFactor:
				{
					return D3D12_BLEND_BLEND_FACTOR;
				}
				case EBlendFactor::InvBlendFactor:
				{
					return D3D12_BLEND_INV_BLEND_FACTOR;
				}
				case EBlendFactor::Src1Color:
				{
					return D3D12_BLEND_SRC1_COLOR;
				}
				case EBlendFactor::InvSrc1Color:
				{
					return D3D12_BLEND_INV_SRC1_COLOR;
				}
				case EBlendFactor::Src1Alpha:
				{
					return D3D12_BLEND_SRC1_ALPHA;
				}
				case EBlendFactor::InvSrc1Alpha:
				{
					return D3D12_BLEND_INV_SRC1_ALPHA;
				}
				default:
				{
					return D3D12_BLEND_ONE;
				}
			}
		}


		D3D12_CULL_MODE	ConvertCullMode(ECullingMode v)
		{
			switch (v)
			{
			case ECullingMode::Front:
				return D3D12_CULL_MODE_FRONT;

			case ECullingMode::Back:
				return D3D12_CULL_MODE_BACK;

			default:
				return D3D12_CULL_MODE_NONE;
			}
		}

		D3D12_FILL_MODE ConvertFillMode(EFillMode v)
		{
			switch (v)
			{
			case EFillMode::Wireframe:
				return D3D12_FILL_MODE_WIREFRAME;

			case EFillMode::Solid:
				return D3D12_FILL_MODE_SOLID;

			default:
				return D3D12_FILL_MODE_SOLID;
			}
		}

		D3D12_STENCIL_OP ConvertStencilOp(EStencilOp v)
		{
			switch (v)
			{
			case EStencilOp::Keep:
				return D3D12_STENCIL_OP_KEEP;

			case EStencilOp::Zero:
				return D3D12_STENCIL_OP_ZERO;

			case EStencilOp::Replace:
				return D3D12_STENCIL_OP_REPLACE;

			case EStencilOp::IncrSat:
				return D3D12_STENCIL_OP_INCR_SAT;

			case EStencilOp::DecrSat:
				return D3D12_STENCIL_OP_DECR_SAT;

			case EStencilOp::Invert:
				return D3D12_STENCIL_OP_INVERT;

			case EStencilOp::Incr:
				return D3D12_STENCIL_OP_INCR;

			case EStencilOp::Decr:
				return D3D12_STENCIL_OP_DECR;

			default:
				return D3D12_STENCIL_OP_KEEP;
			}
		}

		D3D12_COMPARISON_FUNC ConvertComparisonFunc(ECompFunc v)
		{
			switch (v)
			{
				case ECompFunc::Never:
				return D3D12_COMPARISON_FUNC_NEVER;

				case ECompFunc::Less:
				return D3D12_COMPARISON_FUNC_LESS;

				case ECompFunc::Equal:
				return D3D12_COMPARISON_FUNC_EQUAL;

				case ECompFunc::LessEqual:
				return D3D12_COMPARISON_FUNC_LESS_EQUAL;

				case ECompFunc::Greater:
				return D3D12_COMPARISON_FUNC_GREATER;

				case ECompFunc::NotEqual:
				return D3D12_COMPARISON_FUNC_NOT_EQUAL;

				case ECompFunc::GreaterEqual:
				return D3D12_COMPARISON_FUNC_GREATER_EQUAL;

				case ECompFunc::Always:
				return D3D12_COMPARISON_FUNC_ALWAYS;

			default:
				return D3D12_COMPARISON_FUNC_ALWAYS;
			}
		}

		D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertPrimitiveTopologyType(EPrimitiveTopologyType v)
		{
			switch (v)
			{
			case EPrimitiveTopologyType::Point:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

			case EPrimitiveTopologyType::Line:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

			case EPrimitiveTopologyType::Triangle:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

			case EPrimitiveTopologyType::Patch:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

			default:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
			}
		}

		D3D_PRIMITIVE_TOPOLOGY ConvertPrimitiveTopology(ngl::rhi::EPrimitiveTopology v)
		{
			switch (v)
			{
			case EPrimitiveTopology::PointList:
				return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			case EPrimitiveTopology::LineList:
				return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			case EPrimitiveTopology::LineStrip:
				return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			case EPrimitiveTopology::TriangleList:
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case EPrimitiveTopology::TriangleStrip:
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			default:
				assert(false);
				return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			}
		}

		D3D12_FILTER ConvertTextureFilter(ETextureFilterMode v)
		{
			static constexpr D3D12_FILTER table[] =
			{
				D3D12_FILTER_MIN_MAG_MIP_POINT,
				D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,
				D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
				D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_FILTER_ANISOTROPIC,

				D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
				D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
				D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
				D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
				D3D12_FILTER_COMPARISON_ANISOTROPIC
			};
			assert(std::size(table) > static_cast<size_t>(v));
			return table[static_cast<size_t>(v)];
		}

		D3D12_TEXTURE_ADDRESS_MODE ConvertTextureAddressMode(ETextureAddressMode v)
		{
			static constexpr D3D12_TEXTURE_ADDRESS_MODE table[] =
			{
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE
			};
			assert(std::size(table) > static_cast<size_t>(v));
			return table[static_cast<size_t>(v)];
		}

	}
}
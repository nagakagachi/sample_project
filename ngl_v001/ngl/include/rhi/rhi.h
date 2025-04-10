#pragma once

#include <iostream>
#include <vector>
#include <assert.h>

#include "util/types.h"
#include "text/hash_text.h"


// DescriptorSetのCommandListへの設定時のコピーを削減する最適化用マクロ.
#define NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION 1


namespace ngl
{
	namespace rhi
	{
		using ResourceViewName = ngl::text::HashText<32>;

		enum class ERootParameterType : u16
		{
			ConstantBuffer,
			ShaderResource,
			UnorderedAccess,
			Sampler,

			_Max
		};

		// このシステムではRootSignature固定のため各リソースタイプはシェーダステージ毎にレジスタ0から固定数でバインドする.
		// 以下は各リソースタイプの固定数.
		static const u32 k_cbv_table_size = 16;
		static const u32 k_srv_table_size = 48;
		static const u32 k_uav_table_size = 16;
		static const u32 k_sampler_table_size = 16;

		static constexpr u32 RootParameterTableSize(ERootParameterType type)
		{
			const u32 type_size[] =
			{
				k_cbv_table_size,
				k_srv_table_size,
				k_uav_table_size,
				k_sampler_table_size,

				0,
			};
			static_assert(std::size(type_size) - 1 == static_cast<size_t>(ERootParameterType::_Max), "");
			return type_size[static_cast<u32>(type)];
		}


		template<typename T0, typename T1>
		static constexpr bool check_bits(T0 v0, T1 v1)
		{
			return 0 != (v0 & v1);
		}

		enum class EResourceFormat : int
		{
			Format_UNKNOWN,
			Format_R32G32B32A32_TYPELESS,
			Format_R32G32B32A32_FLOAT,
			Format_R32G32B32A32_UINT,
			Format_R32G32B32A32_SINT,
			Format_R32G32B32_TYPELESS,
			Format_R32G32B32_FLOAT,
			Format_R32G32B32_UINT,
			Format_R32G32B32_SINT,
			Format_R16G16B16A16_TYPELESS,
			Format_R16G16B16A16_FLOAT,
			Format_R16G16B16A16_UNORM,
			Format_R16G16B16A16_UINT,
			Format_R16G16B16A16_SNORM,
			Format_R16G16B16A16_SINT,
			Format_R32G32_TYPELESS,
			Format_R32G32_FLOAT,
			Format_R32G32_UINT,
			Format_R32G32_SINT,
			Format_R32G8X24_TYPELESS,
			Format_D32_FLOAT_S8X24_UINT,
			Format_R32_FLOAT_X8X24_TYPELESS,
			Format_X32_TYPELESS_G8X24_UINT,
			Format_R10G10B10A2_TYPELESS,
			Format_R10G10B10A2_UNORM,
			Format_R10G10B10A2_UINT,
			Format_R11G11B10_FLOAT,
			Format_R8G8B8A8_TYPELESS,
			Format_R8G8B8A8_UNORM,
			Format_R8G8B8A8_UNORM_SRGB,
			Format_R8G8B8A8_UINT,
			Format_R8G8B8A8_SNORM,
			Format_R8G8B8A8_SINT,
			Format_R16G16_TYPELESS,
			Format_R16G16_FLOAT,
			Format_R16G16_UNORM,
			Format_R16G16_UINT,
			Format_R16G16_SNORM,
			Format_R16G16_SINT,
			Format_R32_TYPELESS,
			Format_D32_FLOAT,
			Format_R32_FLOAT,
			Format_R32_UINT,
			Format_R32_SINT,
			Format_R24G8_TYPELESS,
			Format_D24_UNORM_S8_UINT,
			Format_R24_UNORM_X8_TYPELESS,
			Format_X24_TYPELESS_G8_UINT,
			Format_R8G8_TYPELESS,
			Format_R8G8_UNORM,
			Format_R8G8_UINT,
			Format_R8G8_SNORM,
			Format_R8G8_SINT,
			Format_R16_TYPELESS,
			Format_R16_FLOAT,
			Format_D16_UNORM,
			Format_R16_UNORM,
			Format_R16_UINT,
			Format_R16_SNORM,
			Format_R16_SINT,
			Format_R8_TYPELESS,
			Format_R8_UNORM,
			Format_R8_UINT,
			Format_R8_SNORM,
			Format_R8_SINT,
			Format_A8_UNORM,
			Format_R1_UNORM,
			Format_R9G9B9E5_SHAREDEXP,
			Format_R8G8_B8G8_UNORM,
			Format_G8R8_G8B8_UNORM,
			Format_BC1_TYPELESS,
			Format_BC1_UNORM,
			Format_BC1_UNORM_SRGB,
			Format_BC2_TYPELESS,
			Format_BC2_UNORM,
			Format_BC2_UNORM_SRGB,
			Format_BC3_TYPELESS,
			Format_BC3_UNORM,
			Format_BC3_UNORM_SRGB,
			Format_BC4_TYPELESS,
			Format_BC4_UNORM,
			Format_BC4_SNORM,
			Format_BC5_TYPELESS,
			Format_BC5_UNORM,
			Format_BC5_SNORM,
			Format_B5G6R5_UNORM,
			Format_B5G5R5A1_UNORM,
			Format_B8G8R8A8_UNORM,
			Format_B8G8R8X8_UNORM,
			Format_R10G10B10_XR_BIAS_A2_UNORM,
			Format_B8G8R8A8_TYPELESS,
			Format_B8G8R8A8_UNORM_SRGB,
			Format_B8G8R8X8_TYPELESS,
			Format_B8G8R8X8_UNORM_SRGB,
			Format_BC6H_TYPELESS,
			Format_BC6H_UF16,
			Format_BC6H_SF16,
			Format_BC7_TYPELESS,
			Format_BC7_UNORM,
			Format_BC7_UNORM_SRGB,
			Format_AYUV,
			Format_Y410,
			Format_Y416,
			Format_NV12,
			Format_P010,
			Format_P016,
			Format_420_OPAQUE,
			Format_YUY2,
			Format_Y210,
			Format_Y216,
			Format_NV11,
			Format_AI44,
			Format_IA44,
			Format_P8,
			Format_A8P8,
			Format_B4G4R4A4_UNORM,
			_MAX,
		};

		// DepthFormatをSrv用に変換. それ以外はそのまま返却.
		inline EResourceFormat depthToColorFormat(EResourceFormat format)
		{
			switch (format)
			{
			case EResourceFormat::Format_D16_UNORM:
				return EResourceFormat::Format_R16_UNORM;
			case EResourceFormat::Format_D24_UNORM_S8_UINT:
				return EResourceFormat::Format_R24_UNORM_X8_TYPELESS;
			case EResourceFormat::Format_D32_FLOAT:
				return EResourceFormat::Format_R32_FLOAT;
			case EResourceFormat::Format_D32_FLOAT_S8X24_UINT:
				assert(false);
				return EResourceFormat::Format_UNKNOWN;
			default:
				return format;
			}
		}
		// DepthFormatなら真
		inline bool isDepthFormat(EResourceFormat format)
		{
			switch (format)
			{
			case EResourceFormat::Format_D16_UNORM:
				return true;
			case EResourceFormat::Format_D24_UNORM_S8_UINT:
				return true;
			case EResourceFormat::Format_D32_FLOAT:
				return true;
			case EResourceFormat::Format_D32_FLOAT_S8X24_UINT:
				assert(false);
				return false;
			default:
				return false;
			}
		}

		enum class EResourceState
		{
			Common,
			General,
			ConstatnBuffer,
			VertexBuffer,
			IndexBuffer,
			RenderTarget,
			ShaderRead,
			UnorderedAccess,
			DepthWrite,
			DepthRead,
			IndirectArgument,
			CopyDst,
			CopySrc,
			RaytracingAccelerationStructure,
			Present,
		};


		enum class EResourceHeapType
		{
			Default,
			Upload,
			Readback,
		};


		enum class EShaderStage
		{
			Vertex,
			Hull,
			Domain,
			Geometry,
			Pixel,

			Compute,

			ShaderLibrary,
			
			_Max,
		};

		// ブレンド要素
		enum class EBlendFactor
		{
			Zero,
			One,
			SrcColor,
			InvSrcColor,
			SrcAlpha,
			InvSrcAlpha,
			DestAlpha,
			InvDestAlpha,
			DestColor,
			InvDestColor,
			SrcAlphaSat,
			BlendFactor,
			InvBlendFactor,
			Src1Color,
			InvSrc1Color,
			Src1Alpha,
			InvSrc1Alpha,
		};	// enum BlendFactor

		// ブレンド関数
		enum class EBlendOp
		{
			Add,
			Subtract,
			RevSubtract,
			Min,
			Max,
		};	// enum BlendOp

		// 比較関数
		enum class ECompFunc
		{
			Never,
			Less,
			Equal,
			LessEqual,
			Greater,
			NotEqual,
			GreaterEqual,
			Always,
		};	// enum CompFunc

		// ステンシル関数
		enum class EStencilOp
		{
			Keep,
			Zero,
			Replace,
			IncrSat,
			DecrSat,
			Invert,
			Incr,
			Decr,
		};	// enum StencilOp

		// ステンシル面方向
		enum class EStencilFace
		{
			Front,
			Back,

			_Max
		};	// enum StencilFace

		// ポリゴン描画モード
		enum class EFillMode
		{
			Wireframe,
			Solid,
		};	// enum FillMode

		// カリング面
		enum class ECullingMode
		{
			None,
			Front,
			Back,
		};	// enum CullingMode

		// テクスチャフィルタ
		enum class ETextureFilterMode
		{
			Min_Point_Mag_Point_Mip_Point,
			Min_Point_Mag_Point_Mip_Linear,
			Min_Point_Mag_Linear_Mip_Point,
			Min_Point_Mag_Linear_Mip_Linear,

			Min_Linear_Mag_Point_Mip_Point,
			Min_Linear_Mag_Point_Mip_Linear,
			Min_Linear_Mag_Linear_Mip_Point,
			Min_Linear_Mag_Linear_Mip_Linear,

			Anisotropic,

			Comp_Min_Point_Mag_Point_Mip_Point,
			Comp_Min_Point_Mag_Point_Mip_Linear,
			Comp_Min_Point_Mag_Linear_Mip_Point,
			Comp_Min_Point_Mag_Linear_Mip_Linear,

			Comp_Min_Linear_Mag_Point_Mip_Point,
			Comp_Min_Linear_Mag_Point_Mip_Linear,
			Comp_Min_Linear_Mag_Linear_Mip_Point,
			Comp_Min_Linear_Mag_Linear_Mip_Linear,

			Comp_Anisotropic,
		};	// enum TextureFilterMode

		// テクスチャラップモード
		enum class ETextureAddressMode
		{
			Repeat,
			Mirror,
			Clamp,
			Border,
			MirrorOnce,
		};	// enum TextureAddressMode

		// 描画トポロジータイプ
		enum class EPrimitiveTopologyType
		{
			Point,
			Line,
			Triangle,
			Patch,
		};	// enum PrimitiveTopologyType

		// 描画トポロジー
		enum class EPrimitiveTopology
		{
			Undefined,
			PointList,
			LineList,
			LineStrip,
			TriangleList,
			TriangleStrip
		};

		struct SampleDesc
		{
			u32 count = 1;
			u32 quality = 0;
		};

		// レンダーターゲットブレンドステート
		struct RenderTargetBlendState
		{
			bool				blend_enable = false;
			EBlendFactor		src_color_blend = EBlendFactor::SrcAlpha;
			EBlendFactor		dst_color_blend = EBlendFactor::InvSrcAlpha;
			EBlendOp			color_op = EBlendOp::Add;
			EBlendFactor		src_alpha_blend = EBlendFactor::SrcAlpha;
			EBlendFactor		dst_alpha_blend = EBlendFactor::DestAlpha;
			EBlendOp			alpha_op = EBlendOp::Add;
			u8					write_mask = static_cast<u8>(~0u);
		};

		// ブレンドステート
		struct BlendState
		{
			bool					alpha_to_coverage_enable = false;
			bool					independent_blend_enable = false;
			RenderTargetBlendState target_blend_states[8] = {};
		};

		// ラスタライザステート
		struct RasterizerState
		{
			EFillMode		fill_mode = EFillMode::Solid;
			ECullingMode	cull_mode = ECullingMode::Back;
			bool			front_counter_clockwise = false;
			int				depth_bias = 0;
			float			depth_bias_clamp = 0.0f;
			float			slope_scale_depth_bias = 0.0f;
			bool			depth_clip_enable = true;
			bool			multi_sample_enable = false;
			bool			antialiased_line_enable = false;
			u32				force_sample_count = 0;
			bool			conservative_raster = false;
		};

		struct DepthStencilOp
		{
			EStencilOp		stencil_fail_op = EStencilOp::Keep;
			EStencilOp		stencil_depth_fail_op = EStencilOp::Keep;
			EStencilOp		stencil_pass_op = EStencilOp::Keep;
			ECompFunc		stencil_func = ECompFunc::Always;
		};

		struct DepthStencilState 
		{
			bool					depth_enable = false;
			bool					depth_write_enable = true; 
			ECompFunc				depth_func = ngl::rhi::ECompFunc::Always;
			bool					stencil_enable = false;
			u8						stencil_read_mask = u8(~0u);
			u8						stencil_write_mask = u8(~0u);
			DepthStencilOp			front_face = {};
			DepthStencilOp			back_face = {};
		};

		struct StreamOutputDesc
		{
			// dummy
			u32				num_entries = 0;
		};

		struct InputElement
		{
			// セマンティック名. インデックス無し.
			const char*		semantic_name	= {};
			// セマンティックインデックス.
			u32				semantic_index	= 0;
			// 現状はD3Dのものを利用. 必要に応じて抽象化.
			EResourceFormat	format			= EResourceFormat::Format_UNKNOWN;
			u32				stream_slot		= 0;
			// slotの1頂点情報内の対応する情報までのByteOffset
			u32				element_offset	= 0;
		};

		struct InputLayout
		{
			const InputElement*		p_input_elements = nullptr;
			u32						num_elements = 0;
		};



		enum class ETextureType : u32
		{
			Texture1D,              ///< 1D texture. Can be bound as render-target, shader-resource and UAV
			Texture2D,              ///< 2D texture. Can be bound as render-target, shader-resource and UAV
			Texture3D,              ///< 3D texture. Can be bound as render-target, shader-resource and UAV
			TextureCube,            ///< Texture-cube. Can be bound as render-target, shader-resource and UAV
			Texture2DMultisample,   ///< 2D multi-sampled texture. Can be bound as render-target, shader-resource and UAV
		};

		enum class EResourceDimension : u32
		{
			Unknown,
			Texture1D,
			Texture2D,
			Texture3D,
			TextureCube,
			Texture1DArray,
			Texture2DArray,
			Texture2DMS,
			Texture2DMSArray,
			TextureCubeArray,
			AccelerationStructure,
			Buffer,

			Count
		};

		struct ResourceBindFlag
		{
			static constexpr u32 None = (1 << 0);///< The resource will not be bound the pipeline. Use this to create a staging resource
			static constexpr u32 ConstantBuffer = (1 << 1);///< The resource will be bound as a constant-buffer
			static constexpr u32 VertexBuffer = (1 << 2);///< The resource will be bound as a vertex-buffer
			static constexpr u32 IndexBuffer = (1 << 3);///< The resource will be bound as a index-buffer
			static constexpr u32 ShaderResource = (1 << 4);///< The resource will be bound as a shader-resource
			static constexpr u32 UnorderedAccess = (1 << 5);///< The resource will be bound as an UAV
			static constexpr u32 RenderTarget = (1 << 6);///< The resource will be bound as a render-target
			static constexpr u32 DepthStencil = (1 << 7);///< The resource will be bound as a depth-stencil buffer
			static constexpr u32 IndirectArg = (1 << 8);///< The resource will be bound as an indirect argument buffer
			//static constexpr u32 AccelerationStructure					= 0x80000000,  ///< The resource will be bound as an acceleration structure
		};

	}
}
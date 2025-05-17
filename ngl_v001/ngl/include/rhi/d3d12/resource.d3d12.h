#pragma once


#include <algorithm>
#include <iostream>
#include <memory>

#include "rhi/rhi.h"
#include "rhi/rhi_object_garbage_collect.h"

#include "rhi/d3d12/rhi_util.d3d12.h"


#include "util/types.h"
#include "text/hash_text.h"

namespace ngl::rhi
{
	class GraphicsCommandListDep;
}

namespace ngl
{
	namespace rhi
	{
		class DeviceDep;


		using RefBufferDep = RhiRef<class BufferDep>;
		using RefTextureDep = RhiRef<class TextureDep>;


		// Buffer
		class BufferDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				ngl::u32			element_byte_size = 0;
				ngl::u32			element_count = 0;
				u32					bind_flag = 0;// bitmask of ngl::rhi::ResourceBindFlag.
				EResourceHeapType	heap_type = EResourceHeapType::Default;
				EResourceState		initial_state = EResourceState::Common; // 既定はCommon. Uploadヒープの場合はGeneralとする.

				void SetupAsConstantBuffer(ngl::u32 size) 
				{
					// CBは大抵CPU書き込みをするため Upload.
					heap_type = rhi::EResourceHeapType::Upload;
					initial_state = rhi::EResourceState::General;// 初期ステートでConstantBuffer指定. (Generic_Read開始でないといけないかもしれない).

					bind_flag = (int)ngl::rhi::ResourceBindFlag::ConstantBuffer;
					element_byte_size = size;
					element_count = 1;
				}
			};

			BufferDep();
			~BufferDep();

			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			void* Map();
			template<typename T>
			T* MapAs() {
				return (T*)Map();
			}
			void Unmap();

			bool IsValid() const { return (nullptr != resource_.Get()); }

			const Desc& GetDesc() const { return desc_; }

			ID3D12Resource* GetD3D12Resource() const;

		public:
			u32 GetBufferSize() const { return allocated_byte_size_; }
			u32 GetElementByteSize() const { return desc_.element_byte_size; }
			u32 getElementCount() const { return desc_.element_count; }

		private:
			Desc	desc_ = {};
			u32		allocated_byte_size_ = 0;

			void*		map_ptr_ = nullptr;

			Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
		};


		// Upload用のピクセルデータ.
		struct TextureUploadSubresourceInfo
		{
			s32		array_index = {};
			s32		mip_index = {};
			s32		slice_index = {};
			
			s32		width = {};
			s32		height = {};
			rhi::EResourceFormat	format = {};
			s32		rowPitch = {};
			s32		slicePitch = {};
			u8*		pixels = {};
		};
		// TextureSubresourceのレイアウト情報.
		struct TextureSubresourceLayoutInfo
		{
			u64 byte_offset = 0;// このSubresourceが配置される先頭からのオフセット
			
			rhi::EResourceFormat format = EResourceFormat::Format_UNKNOWN;
			u32 width = 0;
			u32 height = 0;
			u32 depth = 0;
			u32 row_pitch = 0;
		};
		
		// Texture
		class TextureDep : public RhiObjectBase
		{
		public:
			struct Desc
			{
				EResourceFormat		format = EResourceFormat::Format_UNKNOWN;
				ngl::u32			width = 1;
				ngl::u32			height = 1;
				ngl::u32			depth = 1;
				ngl::u32			mip_count = 1;
				ngl::u32			sample_count = 1;
				ngl::u32			array_size = 1;

				ETextureType			type = ETextureType::Texture2D;

				u32					bind_flag = 0;// bitmask of ngl::rhi::ResourceBindFlag.
				EResourceHeapType	heap_type = EResourceHeapType::Default;
				EResourceState		initial_state = EResourceState::Common;//

				// リソースのデフォルトクリア値を指定する. ハードウェア的に可能なら指定した値によるクリアが高速になる?.
				// ただし指定した場合はクリア値のバリデーションチェックが有効になり異なる値でのクリアで警告がでるようになる.
				bool				is_default_clear_value = true;
				struct DepthStencil
				{
					// クリア値. 生成時指定による高速クリア. ReverseZの場合は 1.0.
					float clear_value = 1.0f;
				} depth_stencil = {};
				struct RenderTarget
				{
					// クリア値. 生成時指定による高速クリア. ReverseZの場合は 1.0 .
					std::array<float, 4> clear_value = {0.0f, 0.0f , 0.0f , 0.0f };
				} rendertarget = {};

			public:
				static void InitializeBase(Desc& out_desc, rhi::ETextureType dimension_type, rhi::EResourceFormat format, bool rtv, bool uav)
				{
					out_desc.type = dimension_type;
					out_desc.format = format;

					out_desc.bind_flag = rhi::ResourceBindFlag::ShaderResource;
					if (rtv)
						out_desc.bind_flag |= rhi::ResourceBindFlag::RenderTarget;
					if (uav)
						out_desc.bind_flag |= rhi::ResourceBindFlag::UnorderedAccess;
				}
				
				static void InitializeAsTexture2D(Desc& out_desc, rhi::EResourceFormat format, u32 w, u32 h, bool rtv, bool uav)
				{
					InitializeBase(out_desc, rhi::ETextureType::Texture2D, format, rtv, uav);
					
					out_desc.width = w;
					out_desc.height = h;
					out_desc.depth = 1;
					out_desc.array_size = 1;
					out_desc.mip_count = 1;
				}
				static void InitializeAsCubemap(Desc& out_desc, rhi::EResourceFormat format, u32 w, u32 h, bool rtv, bool uav)
				{
					InitializeBase(out_desc, rhi::ETextureType::TextureCube, format, rtv, uav);
					
					out_desc.width = w;
					out_desc.height = h;
					out_desc.depth = 1;
					out_desc.array_size = 1;
					out_desc.mip_count = 1;
				}
			};

			TextureDep();
			~TextureDep();

			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			void* Map();
			void Unmap();

			bool IsValid() const { return (nullptr != resource_.Get()); }

			const Desc& GetDesc() const { return desc_; }

			int NumSubresource() const;
			// out_layout_array : TextureSubresourceLayoutInfo[NumSubresource]
			void GetSubresourceLayoutInfo(TextureSubresourceLayoutInfo* out_layout_array, u64& out_total_byte_size) const;

			// UploadBufferであるp_src_buffer上のsrc_layoutに従うSubresourceデータをTextureのsubresource_indexに対応するSubresourceへコピーする.
			void CopyTextureRegion(GraphicsCommandListDep* p_command_list, int subresource_index, const BufferDep* p_src_buffer, const TextureSubresourceLayoutInfo& src_layout);
			
		public:
			ID3D12Resource* GetD3D12Resource() const;
		public:
			u32 GetBufferSize() const { return allocated_byte_size_; }

		public:
			/** Get a mip-level width
			*/
			uint32_t GetWidth(uint32_t mipLevel = 0) const { return (mipLevel == 0) || (mipLevel < desc_.mip_count) ? std::max<>(1U, desc_.width >> mipLevel) : 0; }
			/** Get a mip-level height
			*/
			uint32_t GetHeight(uint32_t mipLevel = 0) const { return (mipLevel == 0) || (mipLevel < desc_.mip_count) ? std::max<>(1U, desc_.height >> mipLevel) : 0; }
			/** Get a mip-level depth
			*/
			uint32_t GetDepth(uint32_t mipLevel = 0) const { return (mipLevel == 0) || (mipLevel < desc_.mip_count) ? std::max<>(1U, desc_.depth >> mipLevel) : 0; }
			/** Get the number of mip-levels
			*/
			uint32_t GetMipCount() const { return desc_.mip_count; }
			/** Get the sample count
			*/
			uint32_t GetSampleCount() const { return desc_.sample_count; }
			/** Get the array size
			*/
			uint32_t GetArraySize() const { return desc_.array_size; }
			/** Get the resource format
			*/
			EResourceFormat GetFormat() const { return desc_.format; }
			/** Get the resource bind flag
			*/
			u32 GetBindFlag() const { return desc_.bind_flag; }
			/** Get the resource Type
			*/
			ETextureType GetType() const { return desc_.type; }

		private:
			Desc	desc_ = {};
			u32		allocated_byte_size_ = 0;

			void* map_ptr_ = nullptr;

			Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
		};





	}
}

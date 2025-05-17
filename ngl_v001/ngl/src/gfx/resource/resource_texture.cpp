
#include "gfx/resource/resource_texture.h"

#include "resource/resource_manager.h"


#include <direct.h>

namespace ngl
{
	namespace gfx
	{
		void ResTexture::RenderThreadInitialize(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist)
		{
			assert(ref_texture_.IsValid());
			
			const rhi::TextureDep::Desc& dst_desc = ref_texture_->GetDesc();
			// ロードしたMetadataから取得したSubresource数と一致しないのであればどこか間違っている.
			assert(ref_texture_->NumSubresource() == static_cast<int>(upload_subresource_info_array.size()));

			u64 dst_byte_size;
			std::vector<rhi::TextureSubresourceLayoutInfo> dst_layout;
			dst_layout.resize(ref_texture_->NumSubresource());
			// Subresouceのレイアウト情報を取得.
			ref_texture_->GetSubresourceLayoutInfo(dst_layout.data(), dst_byte_size);
			
			rhi::RefBufferDep temporal_upload_buffer = {};
			u8* p_upload_buffer_memory = {};
			// TextureUpload用の一時バッファ上メモリを確保.
			res::ResourceManager::Instance().AllocTextureUploadIntermediateBufferMemory(temporal_upload_buffer, p_upload_buffer_memory, dst_byte_size, p_device);
			if(!p_upload_buffer_memory)
			{
				// 一時Buffer上のメモリ確保に失敗.
				std::cout << "[ERROR] Failed to AllocTextureUploadIntermediateBufferMemory." << std::endl;
				assert(p_upload_buffer_memory);
				return;
			}
			// 確保した一時バッファメモリにレイアウトに則ってアップロード用テクスチャデータをコピー.
			res::ResourceManager::Instance().CopyImageDataToUploadIntermediateBuffer(
				p_upload_buffer_memory,
				dst_layout.data(), upload_subresource_info_array.data(), static_cast<s32>(upload_subresource_info_array.size()));

			// State Transition.
			{
				// Copy Dst.
				p_commandlist->ResourceBarrier(ref_texture_.Get(), ref_texture_->GetDesc().initial_state, rhi::EResourceState::CopyDst);
				// upload bufferはGeneralのままでOK.
			}

			// Copy Command. 適切なレイアウトで配置された一時バッファテクスチャデータをAPIでテクスチャへコピー.
			for(int subresource_index = 0; subresource_index < ref_texture_->NumSubresource(); ++subresource_index)
			{
				ref_texture_->CopyTextureRegion(p_commandlist, subresource_index, temporal_upload_buffer.Get(), dst_layout[subresource_index] );
			}

			// State Transition.
			{
				// Initial State の Commonなどに戻すようにした
				p_commandlist->ResourceBarrier(ref_texture_.Get(), rhi::EResourceState::CopyDst, ref_texture_->GetDesc().initial_state);
			}
			
			
			// CPU側メモリ解放.
			this->upload_subresource_info_array = {};
			this->upload_pixel_memory_ = {};
			
		}
	}
}
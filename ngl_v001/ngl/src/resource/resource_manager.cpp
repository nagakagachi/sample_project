
#include "resource/resource_manager.h"

/*
#include "gfx/mesh_loader_assimp.h"
#include "gfx/texture_loader_directxtex.h"
*/

namespace ngl
{
namespace res
{

	ResourceHandleCacheMap::ResourceHandleCacheMap()
	{
	}
	ResourceHandleCacheMap::~ResourceHandleCacheMap()
	{
		//std::cout << "~ResourceMap()" << std::endl;
	}



	void ResourceManager::ResourceDeleter::operator()(Resource* p_res)
	{
		ResourceManager::Instance().OnDestroyResource(p_res);
	}


	ResourceManager::ResourceManager()
	{
	}

	ResourceManager::~ResourceManager()
	{
		{
			// Lock.
			auto lock = std::lock_guard<std::mutex>(res_render_update_mutex_);

			frame_render_update_list_with_handle_.clear();
		}

		ReleaseCacheAll();

	}

	// 全て破棄.
	void ResourceManager::ReleaseCacheAll()
	{
		auto lock = std::lock_guard(res_map_mutex_);

		for (; res_type_map_.begin() != res_type_map_.end();)
		{
			auto* m = &*res_type_map_.begin();

			for (auto ri = m->second->map_.begin(); ri != m->second->map_.end();)
			{
				auto tmpi = ri;
				++ri;

				const auto ref_count = tmpi->second.use_count();
				auto* p_res = tmpi->second.get();
				if (1 < ref_count)
				{
					// 参照カウントがManager管理分の1より大きい場合は警告.
					std::cout << "[~ResourceManager] Undestroyed resources: " << p_res->p_res_->GetFileName() << " (" << ref_count << ") " << std::endl;
				}

				// NOTE.  map_.clear() だと内部で参照が消えた要素が自信をMapから除去するためclear内でのイテレータが無効になってしまうため一つずつ消す.
				m->second->map_.erase(tmpi);
			}

			// Type別Map自体破棄.
			delete m->second;

			// 除去.
			res_type_map_.erase(res_type_map_.begin());
		}
	}

	// リソース破棄. 参照カウンタが完全にゼロになった場合.
	void ResourceManager::OnDestroyResource(Resource* p_res)
	{
		// データベースから除去.
		Unregister(p_res);

		// 完全に破棄. 保持している描画リソースの安全性のために破棄キューに積んでNフレーム後に完全破棄するなども可能.
		delete p_res;
	}



	// システムによりRenderThreadで呼び出される. CommandListに必要な描画コマンドを発行する.
	void ResourceManager::UpdateResourceOnRender(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist)
	{
		// Lock.
		auto lock = std::lock_guard<std::mutex>(res_render_update_mutex_);
		{
			for(auto& e : frame_render_resource_lambda_)
			{
				e(p_commandlist);
			}
			frame_render_resource_lambda_.clear();
			
			for (auto& e : frame_render_update_list_with_handle_)
			{
				if (e)
				{
					// RenderCommand.
					e->p_res_->OnResourceRenderUpdate(p_device, p_commandlist);

					e.reset();// 参照リセット.
				}
			}
			// 次フレーム用に空に.
			frame_render_update_list_with_handle_.clear();
		}
	}

	// Thread Safe.
	detail::ResourceHolderHandle ResourceManager::FindHandle(const char* res_typename, const char* filename)
	{
		ResourceHandleCacheMap* res_map = GetOrCreateTypedCacheMap(res_typename);

		// 対象タイプのみMutexLockで操作.
		auto lock = std::lock_guard(res_map->mutex_);
		if (res_map->map_.end() == res_map->map_.find(filename))
		{
			return {};
		}

		return res_map->map_[filename];
	}


	// Thread Safe.
	void ResourceManager::Register(detail::ResourceHolderHandle& raw_handle)
	{
		ResourceHandleCacheMap* res_map = GetOrCreateTypedCacheMap(raw_handle->p_res_->GetResourceTypeName());

#if 1
		// 対象タイプのみMutexLockで操作.
		auto lock = std::lock_guard(res_map->mutex_);
		{
			// 登録処理.
			// すでに同名登録されている場合は参照カウント減少し, ゼロになったらOnDestroyResourceのフローに入る.
			res_map->map_[raw_handle->p_res_->GetFileName()] = raw_handle;
		}
#endif	
	}
	// Thread Safe.
	void ResourceManager::Unregister(Resource* p_res)
	{
		ResourceHandleCacheMap* res_map = GetOrCreateTypedCacheMap(p_res->GetResourceTypeName());

#if 1
		// 対象タイプのみMutexLockで操作.
		auto lock = std::lock_guard(res_map->mutex_);
		{
			const auto* filename = p_res->GetFileName();
			if (res_map->map_.end() == res_map->map_.find(filename))
			{
				// 存在しないキー.
				return;
			}

			// 登録解除はポインタが一致するかチェック. 同名リソースに別実体が登録されて参照カウントがゼロになるパターンがあるため.
			if (res_map->map_[filename]->p_res_ == p_res)
			{
				// ハンドルをデータベースから解放.
				// ここで参照カウントがゼロになった場合は ResourceDeleter経由でOnDestroyResourceが呼ばれ, 再度Unregisterが呼ばれるがすでにMapにはないので素通りする.
				res_map->map_.erase(filename);
			}
		}
#endif
	}

	// Thread Safe.
	ResourceHandleCacheMap* ResourceManager::GetOrCreateTypedCacheMap(const char* res_typename)
	{
		ResourceHandleCacheMap* res_map = nullptr;
		{
			// Lock.
			auto lock = std::lock_guard(res_map_mutex_);

			if (res_type_map_.end() == res_type_map_.find(res_typename))
			{
				// タイプ用の要素を追加.
				auto new_map = new ResourceHandleCacheMap();
				// 必要な要素追加.
				res_type_map_[res_typename] = new_map;
			}
			assert(res_type_map_.end() != res_type_map_.find(res_typename));

			res_map = res_type_map_[res_typename];
		}
		return res_map;
	}


	// TextureUploadBufferテスト.
	void ResourceManager::AllocTextureUploadIntermediateBufferMemory(rhi::RefBufferDep& ref_buffer, u8*& p_buffer_memory, u64 require_byte_size, rhi::DeviceDep* p_device)
	{
		// UploadBufferの確保関連のMutex.
		auto lock = std::lock_guard<std::mutex>(res_upload_buffer_mutex_);
		
		// Upload一時Buffer生成. 現状は要求1つにつき新規にBufferを生成しているが, プール化と1つのバッファ上にAlignを考慮して配置することで効率化できる.
		rhi::RefBufferDep temporal_upload_buffer = new rhi::BufferDep();
		{
			rhi::BufferDep::Desc upload_buffer_desc = {};
			{
				upload_buffer_desc.heap_type = rhi::EResourceHeapType::Upload;
				upload_buffer_desc.initial_state = rhi::EResourceState::General;// D3DではUploadはGeneral(GenericRead)要求.
				upload_buffer_desc.element_byte_size = rhi::align_to(static_cast<u32>(require_byte_size), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
				upload_buffer_desc.element_count = 1;
			}
			if(!temporal_upload_buffer->Initialize(p_device, upload_buffer_desc))
				assert(false);
		}

		ref_buffer = temporal_upload_buffer;
		// 現状は専用に確保したBufferを先頭から使う. Mapしたまま.
		p_buffer_memory = ref_buffer->MapAs<u8>();
	}
	void ResourceManager::CopyImageDataToUploadIntermediateBuffer(u8* p_buffer_memory,
		const rhi::TextureSubresourceLayoutInfo* p_subresource_layout_array, const rhi::TextureUploadSubresourceInfo* p_subresource_data_array, u32 num_subresource_data) const
	{
		// Subresource毎.
		for(u32 subresource_index = 0; subresource_index < num_subresource_data; ++subresource_index)
		{
			// RowPitchのAlignを考慮してコピー.
			if(p_subresource_layout_array[subresource_index].row_pitch != static_cast<u32>(p_subresource_data_array[subresource_index].rowPitch))
			{
				// 読み取りと書き込みのRowPitchがAlighによってずれている場合はRow毎にコピーする.
				const auto* src_pixel_data = p_subresource_data_array[subresource_index].pixels;
				const auto src_row_pitch = p_subresource_data_array[subresource_index].rowPitch;
				const auto src_slice_pitch = p_subresource_data_array[subresource_index].slicePitch;
				const auto num_row = src_slice_pitch / src_row_pitch;
				for(int row_i = 0; row_i<num_row; ++row_i)
				{
					memcpy(p_buffer_memory + p_subresource_layout_array[subresource_index].byte_offset + (row_i * p_subresource_layout_array[subresource_index].row_pitch),
						src_pixel_data + (row_i * src_row_pitch),
						src_row_pitch);
				}
			}
			else
			{
				// RowPitchが一致している場合はSlice全体コピー.
				memcpy(p_buffer_memory + p_subresource_layout_array[subresource_index].byte_offset, p_subresource_data_array[subresource_index].pixels, p_subresource_data_array[subresource_index].slicePitch);
			}
		}
	}
	
}
}
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

#include <thread>
#include <mutex>


#include "math/math.h"
#include "util/noncopyable.h"
#include "util/singleton.h"

#include "rhi/d3d12/device.d3d12.h"


// resource base.
#include "resource.h"

// resource derived.
#include "gfx/resource/resource_shader.h"

#include "gfx/resource/resource_mesh.h"

#include "gfx/resource/resource_texture.h"

// Res読み込み時の描画スレッド処理を汎用のRenderCommandで実行する.
#include "framework/gfx_render_command_manager.h"

namespace ngl
{
namespace gfx
{
	class ResMeshData;
	class ResShader;
}

namespace res
{

	class ResourceHandleCacheMap
	{
	public:
		ResourceHandleCacheMap();
		~ResourceHandleCacheMap();

		// raw handleでmap保持. Appのリクエストに返す場合は ResourceHandle化して返す.
		std::unordered_map<std::string, detail::ResourceHolderHandle> map_;

		std::mutex	mutex_;
	};

	// Resource管理.
	//	読み込み等.
	//	内部でResourceCacheを持ち参照も保持するため, 外部の参照が無くなったResourceもCacheの参照で破棄されない点に注意.
	//	外部から明示的にCacheからの破棄を要求する仕組みもほしい.
	class ResourceManager : public ngl::Singleton<ResourceManager>
	{
		friend class ResourceDeleter;

	private:
		// ----------------------------------------------------------------------------------------------------------------------------
		// タイプ毎のロード実装.
		
		// ResShader ロード処理実装部.
		bool LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResShader* p_res, gfx::ResShader::LoadDesc* p_desc);
		// ResMeshData ロード処理実装部.
		bool LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResMeshData* p_res, gfx::ResMeshData::LoadDesc* p_desc);
		// ResTextureData ロード処理実装部. DDS or WIC.
		bool LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResTexture* p_res, gfx::ResTexture::LoadDesc* p_desc);

		// ----------------------------------------------------------------------------------------------------------------------------
		
	public:
		ResourceManager();

		~ResourceManager();

		// Cacheされたリソースをすべて解放. ただし外部で参照が残っている場合はその参照が消えるまでは実体は破棄されない.
		void ReleaseCacheAll();
	public:
		// Load.
		// RES_TYPE は ngl::res::Resource 継承クラス.
		template<typename RES_TYPE>
		ResourceHandle<RES_TYPE> LoadResource(rhi::DeviceDep* p_device, const char* filename, typename RES_TYPE::LoadDesc* p_desc);

	public:
		// TextureUpload用の一時Buffer上メモリを確保.
		void AllocTextureUploadIntermediateBufferMemory(rhi::RefBufferDep& ref_buffer, u8*& p_buffer_memory, u64 require_byte_size, rhi::DeviceDep* p_device);
		// TextureUpload用の一時Buffer上に適切に配置されたイメージデータをテクスチャにコピーする.
		void CopyImageDataToUploadIntermediateBuffer(u8* p_buffer_memory, const rhi::TextureSubresourceLayoutInfo* p_subresource_layout_array, const rhi::TextureUploadSubresourceInfo* p_subresource_data_array, u32 num_subresource_data) const;
		
	private:
		void OnDestroyResource(Resource* p_res);

		void Register(detail::ResourceHolderHandle& raw_handle);
		void Unregister(Resource* p_res);

		detail::ResourceHolderHandle FindHandle(const char* res_typename, const char* filename);

	private:
		ResourceHandleCacheMap* GetOrCreateTypedCacheMap(const char* res_typename);

		std::unordered_map<std::string, ResourceHandleCacheMap*>	res_type_map_;

		// タイプ別Map自体へのアクセスMutex. 全体破棄と個別破棄で部分的に再帰するためrecursive_mutexを使用.
		std::recursive_mutex	res_map_mutex_;

		// TextureUploadIntermediateBufferのMutex.
		std::mutex	res_upload_buffer_mutex_;
	private:
		class ResourceDeleter : public res::IResourceDeleter
		{
		public:
			void operator()(Resource* p_res);
		};
		ResourceDeleter deleter_instance_ = {};
	};




	// Load処理Template部.
	template<typename RES_TYPE>
	ResourceHandle<RES_TYPE> ResourceManager::LoadResource(rhi::DeviceDep* p_device, const char* filename, typename RES_TYPE::LoadDesc* p_desc)
	{
		// 登録済みか検索.
		auto exist_handle = FindHandle(RES_TYPE::k_resource_type_name, filename);
		if (exist_handle.get())
		{
			// あれば返却.
			return ResourceHandle<RES_TYPE>(exist_handle);
		}

		// 存在しない場合は読み込み.

		// 新規生成.
		auto p_res = new RES_TYPE();
		res::ResourcePrivateAccess::SetResourceInfo(p_res, filename);


		// Resourceタイプ別のロード処理.
		if (!LoadResourceImpl(p_device, p_res, p_desc))
		{
			delete p_res;
			return {};
		}

		// Handle生成.
		auto handle = ResourceHandle(p_res, &deleter_instance_);
		// 内部管理用RawHandle取得. handleの内部参照カウンタ共有.
		auto raw_handle = ResourcePrivateAccess::GetRawHandle(handle);
		// データベースに登録.
		Register(raw_handle);

		if (p_res->IsNeedRenderThreadInitialize())
		{
			// RenderThreadでの初期化処理を汎用RenderComamndで登録. ハンドル参照を値キャプチャしてカウント保証.
			fwk::PushCommonRenderCommand([raw_handle](fwk::CommonRenderCommandArgRef arg)
			{
				auto device = arg.command_list->GetDevice();
				raw_handle->p_res_->RenderThreadInitialize(device, arg.command_list);
			});
		}

		// 新規ハンドルを生成して返す.
		return handle;
	}
	
}
}

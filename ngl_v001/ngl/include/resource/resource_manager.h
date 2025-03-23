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

// other.



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
		// システムによりRenderThreadで呼び出される. CommandListに必要な描画コマンドを発行する.
		void UpdateResourceOnRender(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist);

	public:
		// Load.
		// RES_TYPE は ngl::res::Resource 継承クラス.
		template<typename RES_TYPE>
		ResourceHandle<RES_TYPE> LoadResource(rhi::DeviceDep* p_device, const char* filename, typename RES_TYPE::LoadDesc* p_desc);

	public:
		// FrameのRenderThreadで実行されるLambdaを登録.
		//	RenderThread先頭, Frameで最初にExecuteされるGraphicsCommandlistを引数に実行されるLambda.
		//	GraphicsCommandListを使用したRender側初期化が必要なオブジェクト全般で便利に利用するため.
		inline void AddFrameRenderUpdateLambda(const std::function<void(rhi::GraphicsCommandListDep*)>& f);

		
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


		// ResourceのRenderUpdateリスト(参照保持のためハンドル).
		std::vector<detail::ResourceHolderHandle> frame_render_update_list_with_handle_;
		std::vector<std::function<void(rhi::GraphicsCommandListDep*)>> frame_render_resource_lambda_;
		// ResourceのRenderUpdateリストのMutex.
		std::mutex	res_render_update_mutex_;

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
		res::ResoucePrivateAccess::SetResourceInfo(p_res, filename);


		// Resourceタイプ別のロード処理.
		if (!LoadResourceImpl(p_device, p_res, p_desc))
		{
			delete p_res;
			return {};
		}

		// Handle生成.
		auto handle = ResourceHandle(p_res, &deleter_instance_);
		// 内部管理用RawHandle取得. handleの内部参照カウンタ共有.
		auto raw_handle = ResoucePrivateAccess::GetRawHandle(handle);
		// データベースに登録.
		Register(raw_handle);

		// 新規リソースをRenderThread初期化リストへ登録.
		if (p_res->IsNeedRenderUpdate())
		{
			auto lock = std::lock_guard<std::mutex>(res_render_update_mutex_);
			frame_render_update_list_with_handle_.push_back(raw_handle);
		}

		// 新規ハンドルを生成して返す.
		return handle;
	}
	
	void ResourceManager::AddFrameRenderUpdateLambda(const std::function<void(rhi::GraphicsCommandListDep*)>& f)
	{
		auto lock = std::lock_guard<std::mutex>(res_render_update_mutex_);
		frame_render_resource_lambda_.push_back(f);
	}
	
	
}
}

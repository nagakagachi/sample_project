#pragma once

#include <memory>
#include <atomic>

#include "util/noncopyable.h"
#include "text/hash_text.h"


#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"


namespace ngl
{
namespace res
{
	class Resource;

	// Resource破棄カスタマイズ.
	class IResourceDeleter
	{
	public:
		IResourceDeleter() {}
		virtual ~IResourceDeleter() {}

		// 破棄オペレータ.
		virtual void operator()(Resource* p_res) = 0;
	};

	// デフォルトdelete版.
	class ResourceDeleterDefault : public IResourceDeleter
	{
	public:
		ResourceDeleterDefault() {}
		~ResourceDeleterDefault() {}

		// 破棄オペレータ.
		void operator()(Resource* p_res);

		static ResourceDeleterDefault& Instance()
		{
			static ResourceDeleterDefault inst = {};
			return inst;
		}
	};


	namespace detail
	{
		class ResourcePtrHolder
		{
		public:
			ResourcePtrHolder() {}
			ResourcePtrHolder(Resource* p, IResourceDeleter* deleter);

			// Resource実体Ptrの破棄.
			~ResourcePtrHolder();

			
			Resource*			p_res_ = nullptr;
			IResourceDeleter*	p_deleter_ = nullptr;
		};

		using ResourceHolderHandle = std::shared_ptr<const ResourcePtrHolder>;
	}



	// Appがリソースを保持する際は必ずこのハンドルで取り扱う. 
	//	参照カウント管理.
	template<typename ResType>
	class ResourceHandle
	{
	public:
		ResourceHandle()
		{}
		ResourceHandle(ResourceHandle& p)
		{
			raw_handle_ = p.raw_handle_;
		}
		ResourceHandle(const ResourceHandle& p)
		{
			raw_handle_ = p.raw_handle_;
		}
		ResourceHandle(detail::ResourceHolderHandle& h)
		{
			raw_handle_ = h;
		}

		ResourceHandle(ResType* p_res, IResourceDeleter* p_deleter = &ResourceDeleterDefault::Instance())
		{
			// 新規リソース実体のハンドルとなるため, 取り扱い注意.
			raw_handle_.reset(new detail::ResourcePtrHolder(p_res, p_deleter));
		}
		~ResourceHandle() {}


		bool IsValid() const
		{
			return nullptr != raw_handle_.get() && nullptr != raw_handle_.get()->p_res_;
		}
		const ResType* Get() const
		{
			return static_cast<const ResType*>(raw_handle_.get()->p_res_);
		}
		const ResType* operator->() const
		{
			return static_cast<const ResType*>(raw_handle_.operator->()->p_res_);
		}
		const ResType& operator*() const
		{
			return *static_cast<const ResType*>(raw_handle_.operator*().p_res_);
		}
		

	private:
		friend class ResoucePrivateAccess;
		detail::ResourceHolderHandle GetRawHandle() { return raw_handle_; }

		// Resouce基底ポインタを保持するオブジェクトの参照カウンタ.
		//	これにより実体をハンドルで共有しつつ, ハンドルの参照が無くなった際の保持オブジェクトのデストラクタで実体の破棄をカスタマイズする.
		detail::ResourceHolderHandle raw_handle_;
	};


	// Resource派生クラスのRenderThread初期化処理インターフェース.
	// 派生クラスは operator(rhi::DeviceDep*, rhi::GraphicsCommandListDep*) を実装する(MeshであればジオメトリバッファのCPUメモリからGPUメモリへのコピーコマンド発行等).
	class IResourceRenderUpdater
	{
	public:
		IResourceRenderUpdater() {}
		virtual ~IResourceRenderUpdater() {}

		// RenderThreadでの初期化が必要なクラスの場合は true を返すようにoverrideする.
		virtual bool IsNeedRenderThreadInitialize() const { return false; }
		// 描画コマンドを伴う初期化がある場合の実装. システムによって自動的にRenderThreadからの呼び出しに登録される.
		virtual void RenderThreadInitialize(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist) {};

	};


	// Resource派生クラス用のクラススコープ内宣言用マクロ.
#define NGL_RES_MEMBER_DECLARE(CLASS_NAME) \
	public:\
		static constexpr char k_resource_type_name[k_resource_type_name_len] = #CLASS_NAME;\
		const char* GetResourceTypeName() const override final { return k_resource_type_name;}
	
	// Resouce基底.
	//	派生クラスはクラススコープで NGL_RES_MEMBER_DECLARE(クラス名) を記述すること.
	class Resource : public NonCopyableTp<Resource>, public IResourceRenderUpdater
	{
	public:
		static constexpr int k_resource_type_name_len = 64;

	public:
		Resource();
		virtual ~Resource();

		// リソースタイプ名. マクロで自動定義.
		virtual const char* GetResourceTypeName() const = 0;


		const char* GetFileName() const { return filename_.c_str(); }
	private:
		friend class ResoucePrivateAccess;

		void SetResourceInfo(const char* filename);

	private:
		std::string filename_ = {};
	};





	// Privateアクセス用.
	class ResoucePrivateAccess
	{
	public:
		static inline void SetResourceInfo(Resource* p_res, const char* filename)
		{
			if (p_res)
				p_res->SetResourceInfo(filename);
		}

		template<typename ResType>
		static inline detail::ResourceHolderHandle  GetRawHandle(ResourceHandle<ResType>& handle)
		{
			return handle.GetRawHandle();
		}
	};
}
}

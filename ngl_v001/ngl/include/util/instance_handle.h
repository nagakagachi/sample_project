#pragma once
#ifndef _NGL_UTIL_INSTANCE_HANDLE
#define _NGL_UTIL_INSTANCE_HANDLE

#include <utility>

#include "util/types.h"

#include "util/singleton.h"

namespace ngl
{
	template<typename T, u16 NUM_INSTANCE>
	class InstanceHandleFactory;


	//
	//	固定長の共有ポインタ的オブジェクトハンドル
	//	
	//		// 以下のようにタイプと個数を指定して置き
	//		typedef ngl::InstanceHandle<double, 128> TestHandle;
	//
	//		TestHandle handle0;  // 既定では無効なハンドルになっている
	//
	//		handle0.newHandle(); // これで新しく有効なハンドルを生成
	//
	//		TestHandle handle1 = handle0;  // のようにするとhandle1とhandle0が同じ実体を共有する
	//
	//		handle0.release(); で参照カウントを減算する
	//		
	//		参照カウントがゼロになった実態はフリーになり、新しいハンドルに割り当て可能になる
	//
	//		実体のクラスはデフォルトコンストラクタを持っている必要があります
	//
	//
	template<typename T, u16 NUM_INSTANCE>
	class InstanceHandle
	{
		typedef InstanceHandleFactory<T, NUM_INSTANCE> Factory;

	public:
		friend Factory;

		InstanceHandle()
		{
		}


		// コピーコンストラクタ
		InstanceHandle(const InstanceHandle& obj)
		{
			*this = obj;
		}
		// 代入
		InstanceHandle& operator=(const InstanceHandle& obj)
		{
			// 今までのハンドルのカウントを減算して
			Release();
			
			// 新しいハンドルをコピーして
			handle_id_ = obj.handle_id_;


			// 新しい方でカウント加算
			Factory::Instance().AddRef(*this);
			return *this;
		}

		// move系
		InstanceHandle(InstanceHandle&& obj)
		{
			*this = std::move(obj);
		}
		InstanceHandle & operator=(InstanceHandle&& obj) noexcept
		{
			// 持っていたハンドルは無効にして
			Release();
			// ハンドルIDだけコピーして
			handle_id_ = obj.handle_id_;
			// ハンドルを委譲するためobj側のハンドルは直接無効化
			obj.handle_id_ = NUM_INSTANCE;
			
			return *this;
		}

		~InstanceHandle()
		{
			// デストラクタで参照カウント減算
			Release();
		}

		T* operator->()
		{
			return Factory::Instance().GetObject(*this);
		}

		// インスタンス取得
		T* Get()
		{
			return Factory::Instance().GetObject(*this);
		}

		bool IsValidHandle()
		{
			return Factory::Instance().IsValidHandle(*this);
		}
		// 新規ハンドル
		bool NewHandle()
		{
			// 現在のハンドルを解放して
			Release();
			// 新規取得
			Factory::Instance().NewHandle(*this);
			return IsValidHandle();
		}
		// ハンドル解放
		void Release()
		{
			// 参照カウント減算して無効なハンドルにする
			Factory::Instance().release(*this);
			handle_id_ = NUM_INSTANCE;
		}

	private:
		u16 handle_id_	= NUM_INSTANCE;
	};

	template<typename T, u16 NUM_INSTANCE>
	class InstanceHandleFactory : public Singleton<InstanceHandleFactory<T, NUM_INSTANCE>>
	{
		typedef InstanceHandle<T, NUM_INSTANCE> HandleType;

	public:
		InstanceHandleFactory()
		{
			for (u32 i = 0; i < NUM_INSTANCE; ++i)
				ref_count_[i] = 0;
		}

		T* GetObject(const HandleType& handle)
		{
			if (IsValidHandle(handle))
				return &instance_[handle.handle_id_];
			return NULL;
		}


		void NewHandle(HandleType& handle)
		{
			handle.handle_id_ = NUM_INSTANCE;
			for (u32 i = 0; i < NUM_INSTANCE; ++i)
			{
				if (0 == ref_count_[i])
				{
					// 参照1で開始
					ref_count_[i] = 1;
					handle.handle_id_ = i;
					break;
				}
			}
		}
		bool IsValidHandle(const HandleType& handle)
		{
			if (NUM_INSTANCE > handle.handle_id_ && 1 <= ref_count_[handle.handle_id_])
				return true;
			return false;
		}
		// 参照カウント加算
		void AddRef(const HandleType& handle)
		{
			if (IsValidHandle(handle))
			{
				++ref_count_[handle.handle_id_];
			}
		}
		// 参照カウント減算
		void release(const HandleType& handle)
		{
			if (IsValidHandle(handle))
			{
				if (0 == --ref_count_[handle.handle_id_])
				{
					// 参照がゼロになった
				}
			}
		}

	private:
		T	instance_[NUM_INSTANCE]		= {};
		u16	ref_count_[NUM_INSTANCE]	= {};
	};
}

#endif // _NGL_UTIL_INSTANCE_HANDLE
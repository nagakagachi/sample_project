#pragma once

#ifndef _NGL_UTIL_SHARED_COUNT_
#define _NGL_UTIL_SHARED_COUNT_

//
//	参照カウンタ
//		既定のnew/deleteを使用しているがそのうちAllocator指定したい
//

#include "types.h"

namespace ngl
{
	// 参照カウンタ基底
	class RefCountCoreBase
	{
	public:
		RefCountCoreBase()
			: count_(1)
		{
		}
		// 継承先でオーバーライド
		virtual ~RefCountCoreBase()
		{
		}
		// 参照カウント加算
		void AddRef()
		{
			++count_;
		}
		void release()
		{
			if (--count_ == 0)
			{
				// 管理ポインタの実削除処理
				Dispose();
				// 参照カウンタの削除
				delete this;
			}
		}
		// 実削除処理
		virtual void Dispose() = 0;

		virtual void* Get() = 0;

	private:
		s32 count_;
	};

	// 参照カウンタ実装
	template<typename T, typename DeleterT>
	class RefCountCoreImplDel
		: public RefCountCoreBase
	{
	public:
		RefCountCoreImplDel(T* ptr, DeleterT deleter)
			: ptr_(ptr), deleter_(deleter)
		{
		}
		void Dispose() override
		{
			// Deleterで削除
			deleter_(ptr_);
		}
		void* Get() override
		{
			return ptr_;
		}
	private:
		T* ptr_;
		DeleterT deleter_;
	};


	// 参照カウンタ
	class SharedCount
	{
	public:
		SharedCount()
			: count_(nullptr)
		{
		}

		// デリータ有り
		template<typename T, typename DeleterT>
		SharedCount(T* ptr, DeleterT deleter)
			: count_(new RefCountCoreImplDel<T, DeleterT>(ptr, deleter))
		{
		}

		~SharedCount()
		{
			if (count_)
				count_->release();
		}

		// 参照加算
		SharedCount(SharedCount const& sc)
			: count_(sc.count_)
		{
			if (count_)
				count_->AddRef();
		}

		// 既存をデクリメント、ソース側をインクリメント
		SharedCount& operator=(SharedCount const& sc)
		{
			if (sc.count_ != count_)
			{
				if (sc.count_)
					sc.count_->AddRef();
				if (count_)
					count_->release();
				count_ = sc.count_;
			}
			return *this;
		}

		// 参照取得
		template<typename T>
		T* Get() const
		{
			if (count_)
				return static_cast<T*>(count_->Get());
			else
				return NULL;
		}

	private:
		// カウンタ実部
		RefCountCoreBase* count_;
	};

}

#endif // _NGL_UTIL_SHARED_COUNT_
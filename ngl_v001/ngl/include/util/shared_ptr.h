#pragma once

//		既定のnew/deleteを使用しているがそのうちAllocator指定したい

#include "shared_count.h"

namespace ngl
{
	// 型定義用
	template<typename T>
	class SharedPtrTypeDef
	{
	public:
		typedef T* TypePtr;
	};
	template<typename T>
	class SharedPtrTypeDef<T[]>
	{
	public:
		typedef T* TypePtr;
	};

	// デフォルトデリータ
	template<typename T>
	class SharedPtrDeleter
	{
	public:
		void operator()(T* ptr) const
		{
			if (nullptr != ptr)
				delete ptr;
		}
	};
	template<typename T>
	class SharedPtrDeleter<T[]>
	{
	public:
		void operator()(T* ptr) const
		{
			if (nullptr != ptr)
				delete[] ptr;
		}
	};

	// 共有ポインタ
	template<typename T>
	class SharedPtr
	{
	public:
		typename typedef SharedPtrTypeDef<T>::TypePtr TypePtr;
		typename typedef SharedPtrDeleter<T> DefaultDeleterT;

		// 任意の型のSharedPtrにメンバ公開
		template<typename U>
		friend class SharedPtr;

	public:

		SharedPtr()
		{}

		
		template<typename U>
		SharedPtr(U* ptr)
			: count_(ptr, DefaultDeleterT())
			, ptr_(ptr)
		{
		}
		template<typename U, typename DeleterT>
		SharedPtr(U* ptr, DeleterT deleter)
			: count_(ptr, deleter)
			, ptr_(ptr)
		{
		}

		// コピーコンストラクタ
		template<typename U>
		SharedPtr(SharedPtr<U> const& sp)
			: count_(sp.count_)
			, ptr_(sp.ptr_)
		{
		}

		template<typename U, typename DeleterT>
		void Reset(U* ptr, DeleterT deleter)
		{
			count_ = SharedCount( ptr, deleter );
			ptr_ = ptr;
		}
		template<typename U>
		void Reset(U* ptr)
		{
			Reset( ptr, DefaultDeleterT() );
		}
		void Reset()
		{
			Reset(nullptr, DefaultDeleterT());
		}

		TypePtr operator->() const
		{
			return ptr_;
		}

		TypePtr get() const
		{
			return ptr_;
		}

	private:
		SharedCount count_		= {};

		// 外部アクセス用
		TypePtr			ptr_	= nullptr;
	};
}

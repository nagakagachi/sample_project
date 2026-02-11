#pragma once
#ifndef _NGL_MEMORY_TLSF_ALLOCATOR_
#define _NGL_MEMORY_TLSF_ALLOCATOR_

/*
	TLSF実装によるアロケータ
		コピーは不許可
		moveは許可


	// C++アロケータ版TLSFテスト
	ngl::memory::TlsfAllocator<ngl::math::Mtx44> tlsfAlloc;
	ngl::memory::TlsfAllocator<ngl::math::Mtx44> tlsfAlloc2;
	tlsfAlloc2.initialize(1024 * 1024);
	tlsfAlloc = std::move(tlsfAlloc2);
	ngl::math::Mtx44* mat0 = tlsfAlloc.allocate(2);
	ngl::math::Mtx44* mat1 = tlsfAlloc.allocate(128);
	tlsfAlloc.deallocate(mat0, 2);
	ngl::math::Mtx44* mat2 = tlsfAlloc.allocate(1);
	tlsfAlloc.deallocate(mat1, 128);
	ngl::math::Mtx44* mat3 = tlsfAlloc.allocate(1);
	tlsfAlloc.deallocate(mat2, 1);
	tlsfAlloc.deallocate(mat3, 1);
	// 解放されていないブロックをチェック(標準出力)
	tlsfAlloc.leakReport();
	tlsfAlloc.destroy();// デストロイ
		
*/

#if 1
#include <memory>
#include "memory/tlsf_allocator_core.h"

#include "util/instance_handle.h"

namespace ngl
{

	namespace memory
	{
		typedef ngl::InstanceHandle<ngl::memory::TlsfAllocatorCore, 32> TlsfAllocatorCoreHandle;

		template<typename T>
		struct TlsfAllocator
		{
			typedef T value_type;

			// 初期化
			// 面倒なので現状では内部でヒープを確保して初期化
			bool Initialize( u32 byteSize )
			{
				if (is_initialized)
				{
					// 前回のinitialize()からdestroy()される前に再度initializeしようとした
					return false;
				}

				// ハンドルの生成
				if (!tlsf_core.NewHandle())
				{
					// アロケータのハンドル生成に失敗
					return false;
				}

				pool_memory = new u8[byteSize];
				if (nullptr == pool_memory)
					return false;
				pool_size = byteSize;

				if (!tlsf_core->Initialize(pool_memory, pool_size))
				{
					Destroy();
					return false;
				}
				is_initialized = true;
				return true;
			}

			// 破棄
			void Destroy()
			{
				tlsf_core->Destroy();
				tlsf_core.Release();// ハンドル解放
				if (nullptr != pool_memory)
				{
					delete pool_memory;
					pool_memory = nullptr;
				}
				pool_size = 0;
				is_initialized = false;
			}

			// メモリリークレポート
			void LeakReport()
			{
				tlsf_core->LeakReport();
			}

			TlsfAllocator()
			{
			}
		
			// コピーは内部で何もしないようにしたい
			template<typename U>
			TlsfAllocator(const TlsfAllocator<U> &) {}
			TlsfAllocator(const TlsfAllocator &) {}
			TlsfAllocator & operator=(const TlsfAllocator &) { return *this; }

			// moveは許可
			TlsfAllocator(TlsfAllocator&& obj)
			{
				*this = std::move(obj);
			}
			// moveオペレータ
			TlsfAllocator & operator=(TlsfAllocator&& obj) noexcept
			{
				// 内容をコピーしつつ移動元は無効にしていく
				pool_size = obj.pool_size;
				obj.pool_size = 0;

				pool_memory = obj.pool_memory;
				obj.pool_memory = nullptr;

				is_initialized = obj.is_initialized;

				tlsf_core = std::move(obj.tlsf_core);

				return *this;
			}


			typedef std::true_type propagate_on_container_copy_assignment;
			typedef std::true_type propagate_on_container_move_assignment;
			typedef std::true_type propagate_on_container_swap;

			bool operator==(const TlsfAllocator & other) const
			{
				return this == &other;
			}
			bool operator!=(const TlsfAllocator & other) const
			{
				return !(*this == other);
			}

			T * allocate(size_t num_to_allocate)
			{
				return static_cast<T*>(tlsf_core->Allocate(sizeof(T)* num_to_allocate));
			}
			void deallocate(T * ptr, size_t num_to_free)
			{
				tlsf_core->Deallocate(ptr);
			}

			// boilerplate that shouldn't be needed, except
			// libstdc++ doesn't use allocator_traits yet
			template<typename U>
			struct rebind
			{
				typedef TlsfAllocator<U> other;
			};
			typedef T * pointer;
			typedef const T * const_pointer;
			typedef T & reference;
			typedef const T & const_reference;
			template<typename U, typename... Args>
			void construct(U * object, Args &&... args)
			{
				new (object)U(std::forward<Args>(args)...);
			}
			template<typename U, typename... Args>
			void construct(const U * object, Args &&... args) = delete;
			template<typename U>
			void Destroy(U * object)
			{
				object->~U();
			}

		private:


			u8*					pool_memory	= nullptr;
			u32					pool_size		= 0;
			bool				is_initialized = false;
			TlsfAllocatorCoreHandle tlsf_core;
		};
	}
}

#endif

#endif //_NGL_MEMORY_TLSF_ALLOCATOR_
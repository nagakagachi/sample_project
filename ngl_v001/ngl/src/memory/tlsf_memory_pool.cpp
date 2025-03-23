
#include "memory/tlsf_memory_pool.h"

namespace ngl
{
	namespace memory
	{
		TlsfMemoryPool::TlsfMemoryPool()
		{
		}
		TlsfMemoryPool::~TlsfMemoryPool()
		{
		}

		bool TlsfMemoryPool::Initialize(void* manage_memory, u32 size)
		{
			Destroy();
			bool success = allocator_.Initialize(manage_memory, size);
			if (success)
			{
				manage_memory_ = manage_memory;
				is_outer_manage_memory_ = true;
			}
			return success;
		}
		// 内部で管理用メモリ確保
		// 解放責任は内部
		bool TlsfMemoryPool::Initialize(u32 size)
		{
			Destroy();
			void* manage_memory = new u8[size];
			if (NULL == manage_memory)
				return false;

			bool success = allocator_.Initialize(manage_memory, size);
			if (success)
			{
				manage_memory_ = manage_memory;
				is_outer_manage_memory_ = false;
			}
			else
			{
				delete[] manage_memory;
				manage_memory = NULL;

			}
			return success;
		}
		void TlsfMemoryPool::Destroy()
		{
			if (!is_outer_manage_memory_)
			{
				delete[] manage_memory_;
				manage_memory_ = NULL;
			}
			is_outer_manage_memory_ = false;

			allocator_.Destroy();
		}

		void TlsfMemoryPool::Deallocate(void* ptr)
		{
			allocator_.Deallocate(ptr);
		}

		// メモリリークのチェック
		// 標準出力にリーク情報を出力する
		void TlsfMemoryPool::LeakReport()
		{
			allocator_.LeakReport();
		}
	}
}

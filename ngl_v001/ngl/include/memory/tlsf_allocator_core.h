#pragma once

#ifndef _NGL_TLSF_ALLOCATOR_CORE_
#define _NGL_TLSF_ALLOCATOR_CORE_
/*
	TLSF アロケータ
*/

#include "util/types.h"
#include "memory/boundary_tag_block.h"

namespace ngl
{
	namespace memory
	{

		class TlsfAllocatorCore
		{
		public:
			template <int N, int M>
			struct power{
				static const int value = N * power<N, M - 1>::value;
			};
			//０の場合だけ特殊化
			template<int N>
			struct power<N, 0>{
				static const int value = 1;
			};

			// 第二レベルの分割サイズの冪乗数 2^N が最小ブロックサイズになる
			// とりあえず最大でも2^5=32分割としておく
			static const u32 SECOND_LEVEL_INDEX_EXP_MAX = 5;
			static const u32 SECOND_LEVEL_INDEX_MAX = power< 2, SECOND_LEVEL_INDEX_EXP_MAX>::value;

			TlsfAllocatorCore();
			~TlsfAllocatorCore();

			// void* mem : 管理メモリを渡す
			//				このメモリの解放責任はこのアロケータにはありません
			// secondLevelExponentiation : 第二レベルの分割数2^nの指数部
			// MaxはSECOND_LEVEL_INDEX_EXP_MAX
			bool Initialize(void* mem, u64 size, u32 second_level_exponentiation = 3);

			// 解放
			void Destroy();


			// メモリリークのチェック
			// 標準出力にリーク情報を出力する
			void LeakReport();


			// 割り当て
			// Deallocateに比べて2倍程度時間がかかっているのであとで調査
			void* Allocate(u64 size);
			// 割り当て解除
			bool Deallocate(void* mem);

		public:
			// 第一レベルインデックス
			s32 GetFirstLevelIndex(u64 require_size);
			// 第二レベル
			s32 GetSecondLevelIndex(u64 require_size, u32 first_level_index, u32 second_level_exp);

			// フリーリストでFLIより大きい最小のFLIを返す
			s32 GetFreeListFirstLevelIndex(u32 first_level_index);
			// フリーリストで指定したFLI内で最小のSLIを返す
			s32 GetFreeListSecondLevelIndex(u32 first_level_index);

		private:
			bool RegisterFreeList(BoundaryTagBlock* block);
			BoundaryTagBlock* RemoveFreeList(BoundaryTagBlock* block);

			// 指定したFLI　SLI　のフリーリストの先頭をリストから除去して返す　なければNULL
			BoundaryTagBlock* RemoveFreeListTop(u32 first_level_index, u32 sli);

			// fli sliに対してblock分割可能なら分割して残りの部分を返す
			BoundaryTagBlock* DivideBlock(BoundaryTagBlock* block, u32 size);

		private:
			void* head_;
			u64 size_;
			u32 second_level_exponentiation_;

			// 管理領域の先頭と末尾にマーカーとしてからのタグを仕込む 
			BoundaryTagBlock*	head_tag_;
			BoundaryTagBlock*	tail_tag_;

			u64					free_list_bit_fli_;// 0-63 の各第一レベルフリーリストの有無を示すビット
			u32					free_list_bit_sli_[64];// 対応する第一レベル内部の第二レベルフリーリストの有無を示すビット

			// 分割幅レベル以下の要素は使われないけどまあいいか　
			BoundaryTagBlock*	free_list_[64][SECOND_LEVEL_INDEX_MAX];
		};
	}
}

#endif // _NGL_TLSF_ALLOCATOR_CORE_
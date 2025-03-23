
#include "memory/tlsf_allocator_core.h"
#include "util/bit_operation.h"

#include <iostream>
#ifdef _DEBUG
#include <assert.h>
#endif

namespace ngl
{
	namespace memory
	{
		TlsfAllocatorCore::TlsfAllocatorCore()
			: head_(nullptr)
			, size_(0)
			, second_level_exponentiation_(0)
			, free_list_bit_fli_(0)
		{
			memset(free_list_bit_sli_, 0x00, sizeof(free_list_bit_sli_));
			memset(free_list_, 0x00, sizeof(free_list_));
		}
		TlsfAllocatorCore::~TlsfAllocatorCore()
		{
		}
		bool TlsfAllocatorCore::Initialize(void* mem, u64 size, u32 second_level_exponentiation)
		{
			// 先頭・末尾・最初のフリーリスト要素　の3つ分の最低限必要なサイズを計算
			u32 manageInfoDataSize = BoundaryTagBlock::GetAppendInfoDataSize() * 3;
			// 第二レベルの分割数に応じた最小のデータ部サイズ

			u32 sliExp = SECOND_LEVEL_INDEX_EXP_MAX < second_level_exponentiation
				? SECOND_LEVEL_INDEX_EXP_MAX : second_level_exponentiation;

			u32 minDataSize = 1;
			for (u32 i = 0; i < sliExp; ++i)
				minDataSize *= 2;

			u64 total_size_tmp = static_cast<u64>(manageInfoDataSize) + static_cast<u64>(minDataSize);
			if (nullptr == mem || total_size_tmp > size || 0 >= sliExp)
				return false;
			head_ = mem;
			size_ = size;
			second_level_exponentiation_ = sliExp;

			// 最初と最後にダミーのタグを仕込む
			head_tag_ = BoundaryTagBlock::Placement(mem, 0);// データサイズ0で先頭に配置
			// データサイズ0で末尾に配置 ヘッドと同じサイズなので流用
			tail_tag_ = BoundaryTagBlock::Placement(mem, 0, static_cast<u32>(size - head_tag_->GetAllSize()));

			// マージしないためにダミーはフラグをON
			head_tag_->SetIsUsed(true);
			tail_tag_->SetIsUsed(true);

			// フラグをリセット
			free_list_bit_fli_ = 0;
			memset(free_list_bit_sli_, 0x00, sizeof(free_list_bit_sli_));
			memset(free_list_, 0x00, sizeof(free_list_));


			// 残った部分を最初の唯一のブロックとする
			// 残ったサイズは先頭、末尾とこのブロック自体の管理情報サイズを除いたもの
			u32 first_data_size = static_cast<u32>(size)-head_tag_->GetAllSize() - tail_tag_->GetAllSize() - BoundaryTagBlock::GetAppendInfoDataSize();
			BoundaryTagBlock* first_block = BoundaryTagBlock::Placement(mem, first_data_size, head_tag_->GetAllSize());

			RegisterFreeList(first_block);

			return true;
		}
		// 解放
		//		特に削除処理はしないが管理用情報などをクリア
		void TlsfAllocatorCore::Destroy()
		{
			head_ = nullptr;
			size_ = 0;
			second_level_exponentiation_ = 0;
			free_list_bit_fli_ = 0;
			memset(free_list_bit_sli_, 0x00, sizeof(free_list_bit_sli_));
			memset(free_list_, 0x00, sizeof(free_list_));
		}

		// メモリリークのチェック
		// 標準出力にリーク情報を出力する
		void TlsfAllocatorCore::LeakReport()
		{
			// 管理メモリ先頭からひとつづつブロックをたどって行って使用中のものがあれば出力
			u8* cur = reinterpret_cast<u8*>(head_);
			u8* end = cur + size_;
			for (; cur < end ;)
			{
				BoundaryTagBlock* block = reinterpret_cast<BoundaryTagBlock*>(cur);

				// データサイズが0のものは管理用の先頭と終端タグなので無視
				if (0 < block->GetDataSize() && block->IsUsed())
				{
					std::cout << "[TlsfAllocatorCore] Memory Leak !" << std::endl;
					std::cout << "	dataSize : " << block->GetDataSize() << std::endl;
				}

				cur = cur + block->GetAllSize();
			}
		}



		// 割り当て
		void* TlsfAllocatorCore::Allocate(u64 size)
		{
			// FLI-SLIに一致するフリーブロックがあればそれを割り当て
			// 無ければビットフラグチェックをして大き目のブロックを割り当てる
			// サイズが前方タグ一つ後方タグ一つ分＋最小割り当てメモリサイズ　を残して分割できる場合は
			// フリーブロックを分割して残った部分を適切なフリーリストに登録する
			if (0 == size)
				return NULL;

			// 強制的に最小サイズ以上にする
			const u32 min_size = 1 << second_level_exponentiation_;
			size = size < min_size ? min_size : size;

			s32 fli = GetFirstLevelIndex(size);
			s32 sli = GetSecondLevelIndex(size, fli, second_level_exponentiation_);

			if (0 != (free_list_bit_fli_ & ((~u64(0x00)) << fli)))
			{
				// 第一インデックスに空きあり
				if (0 != (free_list_bit_sli_[fli] & (u64(1) << sli)))
				{
					// 第二インデックスに一致するフリーブロック発見
					BoundaryTagBlock* block = RemoveFreeListTop(fli, sli);

					// フリーリストから取り出して利用状態になっているブロックの管理も必要だろうか？

					// 使用状態にする
					block->SetIsUsed(true);

					// 返す
					return block->GetDataPtr();
				}
				else
				{
					// 第二インデックスに空きがなかったので同一第一カテゴリでの空きを探す
					s32 up_level_fli = GetFreeListFirstLevelIndex(fli);
					if (0 <= up_level_fli)
					{
						s32 up_level_sli = GetFreeListSecondLevelIndex(up_level_fli);
						if (0 <= up_level_sli)
						{
							// 第二インデックスに一致するフリーブロック発見
							// ついでに　RemoveFreeListTop　の中でフリーリストビットの操作もされている
							BoundaryTagBlock* block = RemoveFreeListTop(up_level_fli, up_level_sli);

							// アロケート時間の30％くらいがここの分割？
							// 分割できるなら分割
							BoundaryTagBlock* div_block = DivideBlock(block, static_cast<u32>(size));
							if (NULL != div_block)
							{
								RegisterFreeList(div_block);
							}

							// 使用状態にする
							block->SetIsUsed(true);

							// 返す
							return block->GetDataPtr();
						}
					}
				}
			}

			return NULL;
		}

		// 割り当て解除
		bool TlsfAllocatorCore::Deallocate(void* mem)
		{
			if (NULL == mem)
				return false;
			// 渡されたメモリの前方 sizeof(BoundaryTagBlock) の位置にブロックが配置されているはずなので
			// それの使用フラグをfalseとして割り当て解除する
			// さらにブロックの前後をチェックして未割当ブロックがある場合はそれらとマージしてフリーリストに登録する

			u8* data_head = reinterpret_cast<u8*>(mem);

			// 面倒なことしているがこれはアライメントのためにブロックの先頭のすぐ後にデータ部がない場合があるため

			// 直前の4バイトにデータ部サイズがある
			u32 data_size = *reinterpret_cast<u32*>(data_head - sizeof(u32));

			// データ部の先頭からデータ部サイズ分オフセットした位置にブロックサイズがある
			u32 block_size = *reinterpret_cast<u32*>(data_head + data_size);

			// データサイズ + データサイズの後ろのブロックサイズ部u32 - ブロックサイズ でブロックの先頭が得られる
			s64 offset_to_block = s64(data_size) + BoundaryTagBlock::GetTailTagSize() - s64(block_size);
			BoundaryTagBlock* block = reinterpret_cast<BoundaryTagBlock*>(data_head + offset_to_block);

			u8* block_head = reinterpret_cast<u8*>(block);

			//前方ブロック
			BoundaryTagBlock* prev_block = reinterpret_cast<BoundaryTagBlock*>(
				block_head - *reinterpret_cast<u32*>(block_head - sizeof(u32))
				);
			// 後方ブロック
			BoundaryTagBlock* nextBlock = reinterpret_cast<BoundaryTagBlock*>(
				block_head + block_size
				);

			BoundaryTagBlock* merge_head = block;
			u32 merge_block_size = block_size;
			if (!prev_block->IsUsed())
			{
				// 前方ブロックをフリーリストから除去
				RemoveFreeList(prev_block);
				// 
				merge_block_size += prev_block->GetAllSize();

				// 前方ブロックがマージ対象ならマージの先頭を前方ブロックにする
				merge_head = prev_block;
			}
			if (!nextBlock->IsUsed())
			{
				// 後方ブロックをフリーリストから除去
				RemoveFreeList(nextBlock);
				// 
				merge_block_size += nextBlock->GetAllSize();
			}

			// マージ
			block = BoundaryTagBlock::Placement(merge_head, merge_block_size - BoundaryTagBlock::GetAppendInfoDataSize());

			// マージが必要なら完了したはずなのでフリーリストに登録
			block->SetIsUsed(false);
			RegisterFreeList(block);

			return true;
		}

		bool TlsfAllocatorCore::RegisterFreeList(BoundaryTagBlock* block)
		{
			s32 fli = GetFirstLevelIndex(block->GetDataSize());
			s32 sli = GetSecondLevelIndex(block->GetDataSize(), fli, second_level_exponentiation_);

			//リストに挿入
			if (nullptr == free_list_[fli][sli])
			{
				free_list_[fli][sli] = block;
			}
			else
			{
				free_list_[fli][sli]->InsertNext(block);
			}

			// フリーリストビット操作
			free_list_bit_fli_ |= (u64(1) << fli);

			free_list_bit_sli_[fli] |= (1 << sli);

			return true;
		}
		BoundaryTagBlock* TlsfAllocatorCore::RemoveFreeList(BoundaryTagBlock* block)
		{
			s32 fli = GetFirstLevelIndex(block->GetDataSize());
			s32 sli = GetSecondLevelIndex(block->GetDataSize(), fli, second_level_exponentiation_);

			if (0 != (free_list_bit_fli_&(u64(1) << fli)) && 0 != (free_list_bit_sli_[fli] & (u64(1) << sli)))
			{
				// フリーリストに直接保持されている場合はNextを登録して自分を除去
				if (free_list_[fli][sli] == block)
				{
					free_list_[fli][sli] = block->GetNext();
				}
				block->Remove();

				// ビットマスクを操作
				// NULLの場合にマスク
				free_list_bit_sli_[fli] &= ~(u64(NULL == free_list_[fli][sli]) << sli);
				free_list_bit_fli_ &= ~(u64(0 == free_list_bit_sli_[fli]) << fli);

				return block;
			}
			return NULL;
		}
		BoundaryTagBlock* TlsfAllocatorCore::RemoveFreeListTop(u32 fli, u32 sli)
		{
			return RemoveFreeList(free_list_[fli][sli]);
		}
		// block分割可能なら分割して残りの部分を　divBlock　に
		BoundaryTagBlock* TlsfAllocatorCore::DivideBlock(BoundaryTagBlock* block, u32 size)
		{
			u32 start_data_size = block->GetDataSize();

			u32 need_size = size + BoundaryTagBlock::GetHeadTagSize() + BoundaryTagBlock::GetTailTagSize();

			//　そもそもデータ部に別のブロックが入るのか
			if (start_data_size < need_size)
			{
				return NULL;
			}
			// startDataSize > 分割に必要な最小限サイズが分かったところで分割で残る部分のサイズをチェック
			u32 div_data_size = start_data_size - need_size;

			// 残る部分のサイズが最小分割サイズある?(第二レベルインデックスの最小分割)
			if (SECOND_LEVEL_INDEX_MAX > div_data_size)
			{
				return NULL;
			}

			// 先頭をリプレース
			block = BoundaryTagBlock::Placement(block, size);

			BoundaryTagBlock* div_block = BoundaryTagBlock::Placement(block, div_data_size, block->GetAllSize());
			return div_block;
		}

		s32 TlsfAllocatorCore::GetFirstLevelIndex(u64 require_size)
		{
			return MostSignificantBit64(require_size);
		}
		// 第二レベル
		s32 TlsfAllocatorCore::GetSecondLevelIndex(u64 require_size, u32 fli, u32 second_level_exp)
		{
			// 最上位ビット未満のビット列だけを有効にするマスク
			const unsigned mask = (1 << fli) - 1;   // 1000 0000 -> 0111 1111

			// 右へのシフト数を算出
			const unsigned rs = fli - second_level_exp;	// 7 - 3 = 4 （8分割ならN=3です）

			// 引数sizeにマスクをかけて、右へシフトすればインデックスに
			return (require_size & mask) >> rs;
		}

		// フリーリストで指定レベルより大きい最小のFLIを返す
		s32 TlsfAllocatorCore::GetFreeListFirstLevelIndex(u32 fli)
		{
			u32 all1 = ~u32(0x00);
			u32 mask = all1 << fli;
			u32 find = mask & free_list_bit_fli_;
			return LeastSignificantBit64(find);
		}
		// フリーリストで指定したFLI内で最小のSLIを返す
		s32 TlsfAllocatorCore::GetFreeListSecondLevelIndex(u32 fli)
		{
			s32 sli = LeastSignificantBit64(free_list_bit_sli_[fli]);
			return sli;
		}
	}
}
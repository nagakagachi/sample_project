#pragma once

#ifndef _NGL_BOUNDARY_TAG_BLOCK_
#define _NGL_BOUNDARY_TAG_BLOCK_


#include "util/types.h"
#include <new>

namespace ngl
{

	class BoundaryTagBlock
	{
	public:
		BoundaryTagBlock();
		BoundaryTagBlock(u8* data, u32 size);
		~BoundaryTagBlock();

		// 初期化
		void Initialize(u8* data, u32 size);

		// データ部
		u8* GetDataPtr() const;

		// ブロック占有サイズの格納されたポインタ
		u32* GetTailTagPtr() const;

		// オブジェクトが占有する全サイズを取得
		u32 GetAllSize() const;

		// データ部のサイズ
		u32 GetDataSize() const;


		// メモリ管理用に付加するデータのサイズ　データ部のサイズ以外に必要な部分のサイズ
		// 先頭と末尾のタグサイズの合計
		static inline u32 GetAppendInfoDataSize()
		{
			return GetHeadTagSize() + GetTailTagSize();
		}

		// メモリ管理用の先頭タグサイズ
		static inline u32 GetHeadTagSize()
		{
			return sizeof(BoundaryTagBlock)+sizeof(u32); // サイズ部もずらした
		}
		// メモリ管理用の末端タグサイズ
		static inline u32 GetTailTagSize()
		{
			return sizeof(u32);
		}

		void SetIsUsed( bool f );
		bool IsUsed() const;

		// 渡されたオブジェクトを自身の後ろに追加する
		BoundaryTagBlock* InsertNext(BoundaryTagBlock* new_object);
		// 渡されたオブジェクトを自身の前に追加する
		BoundaryTagBlock* InsertPrev(BoundaryTagBlock* new_object);
		// リストから離脱
		BoundaryTagBlock* Remove();

		BoundaryTagBlock* GetNext() const;
		BoundaryTagBlock* GetPrev() const;

		// 指定されたメモリにBoundaryTagオブジェクトを配置して初期化しそのポインタとして返す
		static BoundaryTagBlock* Placement(void* placement_memory, u32 dataSize, u32 offset = 0);

	protected:
		u8* data_head_ptr_			= nullptr;// データ部の先頭へのポインタ

		BoundaryTagBlock* prev_		= nullptr;// 双方向リスト用
		BoundaryTagBlock* next_		= nullptr;

		// データサイズ部へのポインタ
		u32*	data_size_tag_ptr_	= nullptr;
		// 使用フラグ
		u8		is_used_ = 0;
	};
}

#endif // _NGL_BOUNDARY_TAG_BLOCK_
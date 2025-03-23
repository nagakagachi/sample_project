
#include "memory/boundary_tag_block.h"

namespace ngl
{

	// 指定されたメモリにBoundaryTagオブジェクトを配置して初期化しそのポインタとして返す
	BoundaryTagBlock* BoundaryTagBlock::Placement(void* placement_memory, u32 data_size, u32 offset)
	{
		u8* m = reinterpret_cast<u8*>(placement_memory);

		u8* headPtr = m + offset;

		// アライメント対応はここでやる 
		BoundaryTagBlock* obj = new(headPtr)BoundaryTagBlock(headPtr + GetHeadTagSize(), data_size);
		return obj;
	}

	BoundaryTagBlock::BoundaryTagBlock()
	{
	}

	BoundaryTagBlock::BoundaryTagBlock(u8* data, u32 size)
	{
		Initialize(data, size);
	}

	BoundaryTagBlock::~BoundaryTagBlock()
	{
		data_head_ptr_		= nullptr;
		prev_				= nullptr;
		next_				= nullptr;
		data_size_tag_ptr_	= nullptr;
		is_used_			= 0;
	}

	void BoundaryTagBlock::Initialize(u8* data, u32 size)
	{
		data_head_ptr_ = data;
	
		// データ部の前方4バイトにサイズが書き込まれる
		data_size_tag_ptr_ = reinterpret_cast<u32*>( data - sizeof(u32) );
		(*data_size_tag_ptr_) = size;
	
		u32* endTag = GetTailTagPtr();
		*endTag = GetAppendInfoDataSize() + GetDataSize();

		is_used_ = false;
		prev_ = next_ = nullptr;
	}

	u8* BoundaryTagBlock::GetDataPtr() const
	{
		return data_head_ptr_;
	}

	// ブロック占有サイズの格納されたポインタ
	u32* BoundaryTagBlock::GetTailTagPtr() const
	{
		return reinterpret_cast<u32*>(data_head_ptr_ + GetDataSize());
	}

	// データ部のサイズ
	u32 BoundaryTagBlock::GetDataSize() const
	{
		return *data_size_tag_ptr_;
	}
	// オブジェクトが占有する全サイズを取得
	u32 BoundaryTagBlock::GetAllSize() const
	{
		return *GetTailTagPtr();
	}

	void BoundaryTagBlock::SetIsUsed(bool f)
	{
		is_used_ = f;
	}
	bool BoundaryTagBlock::IsUsed() const
	{
		return 0!=is_used_;
	}

	// 渡されたオブジェクトを自身の後ろに追加する
	BoundaryTagBlock* BoundaryTagBlock::InsertNext(BoundaryTagBlock* new_object)
	{
		new_object->next_ = next_;
		new_object->prev_ = this;
		if (nullptr != next_)
			next_->prev_ = new_object;
		next_ = new_object;
		return this;
	}
	// 渡されたオブジェクトを自身の前に追加する
	BoundaryTagBlock* BoundaryTagBlock::InsertPrev(BoundaryTagBlock* new_object)
	{
		new_object->next_ = this;
		new_object->prev_ = prev_;
		if (nullptr != prev_)
			prev_->next_ = new_object;
		prev_ = new_object;
		return this;
	}
	// リストから離脱
	BoundaryTagBlock* BoundaryTagBlock::Remove()
	{
		if (nullptr != next_)
			next_->prev_ = prev_;
		if (nullptr != prev_)
			prev_->next_ = next_;
		prev_ = next_ = nullptr;
		return this;
	}

	BoundaryTagBlock* BoundaryTagBlock::GetNext() const
	{
		return next_;
	}
	BoundaryTagBlock* BoundaryTagBlock::GetPrev() const
	{
		return prev_;
	}
}
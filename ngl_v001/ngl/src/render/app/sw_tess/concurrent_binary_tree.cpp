/*
    concurrent_binary_tree.cpp
    完全二分木ビットフィールドクラスの実装
*/

#include "render/app/sw_tess/concurrent_binary_tree.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace ngl::render::app
{
    void ConcurrentBinaryTreeU32::Initialize(uint32_t require_leaf_count)
    {
        // 以上の最小の2の冪
        const uint32_t min_large_power_of_2 = 1u << (ngl::MostSignificantBit32(require_leaf_count-1)+1);
        // require_leaf_countのリーフの完全二分木のノードを格納するためのサイズ.   
        const auto required_packed_leaf_count = std::max(1u, min_large_power_of_2 >> LeafTypePackedIndexShift);
        // 全ノードを格納可能なサイズの木の深さ と インデックス計算簡易化のため1ベースインデックスのために +2
        const auto packed_node_depth = ngl::MostSignificantBit32(required_packed_leaf_count) + 1;

        const auto packed_node_count = 1u << packed_node_depth;
        
        packed_leaf_count_ = (1 << (packed_node_depth-1));
        packed_leaf_offset_ = packed_node_count >> 1;

        cbt_node_.resize(packed_node_count);
            
        Clear();
    }

    void ConcurrentBinaryTreeU32::Clear()
    {
        // Leafの範囲のみクリア
        memset(&(cbt_node_[packed_leaf_offset_]), 0, packed_leaf_count_ * sizeof(LeafType));
        
        // 合計値クリア.
        cbt_node_[SumValueLocation] = 0;
    }

    void ConcurrentBinaryTreeU32::SetBit(uint32_t index, uint32_t bit)
    {
        const uint32_t packed_leaf_node_index = (index >> LeafTypePackedIndexShift);
        const uint32_t packed_leaf_bit_location = (LeafTypeBitLocalIndexMask & index);
        const uint32_t packed_leaf_node_location = packed_leaf_offset_ + packed_leaf_node_index;
        const uint32_t bit_pattern = (1 << packed_leaf_bit_location);

        if(0 != bit)
        {
            cbt_node_[packed_leaf_node_location] |= bit_pattern;
        }
        else
        {
            cbt_node_[packed_leaf_node_location] &= ~bit_pattern;
        }
    }

    uint32_t ConcurrentBinaryTreeU32::GetBit(uint32_t index) const
    {
        const uint32_t packed_leaf_node_index = (index >> LeafTypePackedIndexShift);
        const uint32_t packed_leaf_bit_location = (LeafTypeBitLocalIndexMask & index);
        const uint32_t packed_leaf_node_location = packed_leaf_offset_ + packed_leaf_node_index;

        return (cbt_node_[packed_leaf_node_location] >> packed_leaf_bit_location) & 0x01;
    }

    uint32_t ConcurrentBinaryTreeU32::GetSum() const
    {
        return cbt_node_[SumValueLocation];// 1ベースのインデックス付け.
    }

    void ConcurrentBinaryTreeU32::SumReduction()
    {
        {
            // Leafのbitカウント.
            const auto leaf_parent_start = packed_leaf_offset_ >> 1;
            for(uint32_t i = 0; i < (packed_leaf_count_ >> 1); ++i)
            {
                const auto target_node_location = leaf_parent_start + i;
                const auto bit_count_leaf_2 = ngl::Count32bit(cbt_node_[((target_node_location) << 1) + 0]) + ngl::Count32bit(cbt_node_[(target_node_location << 1) + 1]);
                cbt_node_[target_node_location] = bit_count_leaf_2;
            }
        }

        // bitカウントよりも親の通常バイナリツリー合計.
        for(int d = 2; d <= ngl::MostSignificantBit32(packed_leaf_count_); ++d)
        {
            const auto leaf_parent_start = packed_leaf_offset_ >> d;
            for(uint32_t i = 0; i < (packed_leaf_count_ >> d); ++i)
            {
                const auto target_node_location = leaf_parent_start + i;
                const auto bit_count_leaf_2 = cbt_node_[((target_node_location) << 1) + 0] + cbt_node_[(target_node_location << 1) + 1];
                cbt_node_[target_node_location] = bit_count_leaf_2;
            }
        }
    }

    // uint32_tの値とインデックス値を引数に取り, uint32_tの下位からインデックス番目に現れる 1 のビットの位置を返す関数
    int Find_ith_Bit1_in_u32(uint32_t value, int index)
    {
        // value: 検索対象のu32値
        // index: 下位からi番目の1（0-based）
        // 戻り値: ビット位置（0-based, 存在しなければ-1）
        int count = 0;
        for (int bit = 0; bit < 32; ++bit)
        {
            if ((value >> bit) & 0x1)
            {
                if (count == index)
                    return bit;
                ++count;
            }
        }
        return -1;
    }

    // 下位から i番目 の 1 の位置を検索. SumReduction後に使用可能.
    int ConcurrentBinaryTreeU32::Find_ith_Bit1(uint32_t index)
    {
        if(0 == GetSum())
        return -1;

        // CBT: 完全二分木のノード配列（cbt_node_）
        // index: i番目の1（0-based）
        // 戻り値: ビット位置（0-based, 存在しなければ-1）
        const uint32_t D = ngl::MostSignificantBit32(packed_leaf_count_); // 木の深さ
        uint32_t bitID = 1; // 1ベース
        while (bitID < (1u << D))
        {
            bitID = bitID << 1;
            if (index >= cbt_node_[bitID])
            {
                index -= cbt_node_[bitID];
                bitID += 1;
            }
        }
        const int leaf_pos = static_cast<int>(bitID - (1u << D));
        const LeafType bit_field = (cbt_node_[packed_leaf_offset_ + leaf_pos]);
        if(0 <= leaf_pos && 0 != bit_field)
        {
            const int local_bit_pos = Find_ith_Bit1_in_u32(bit_field, index);
            assert(0 <= local_bit_pos);
            return local_bit_pos + (leaf_pos * LeafTypeBitWidth);
        }

        return -1;
    }

    // 下位から i番目 の 0 の位置を検索. SumReduction後に使用可能.
    int ConcurrentBinaryTreeU32::Find_ith_Bit0(uint32_t index)
    {
        if(NumLeaf() == GetSum())
            return -1;
        
        // CBT: 完全二分木のノード配列（cbt_node_）
        // index: i番目の0（0-based）
        // 戻り値: ビット位置（0-based, 存在しなければ-1）
        const uint32_t D = ngl::MostSignificantBit32(packed_leaf_count_); // 木の深さ
        uint32_t bitID = 1; // 1ベース
        uint32_t c = NumLeaf() >> 1;//1u << (D - 1);
        while (bitID < (1u << D))
        {
            bitID = bitID << 1;
            if (index >= (c - cbt_node_[bitID]))
            {
                index -= (c - cbt_node_[bitID]);
                bitID += 1;
            }
            c = c >> 1;
        }
        const int leaf_pos = static_cast<int>(bitID - (1u << D));
        // 反転ビットで1を探す.
        const LeafType bit_field = ~(cbt_node_[packed_leaf_offset_ + leaf_pos]);
        if(0 <= leaf_pos && 0 != bit_field)
        {
            const int local_bit_pos = Find_ith_Bit1_in_u32(bit_field, index);
            assert(0 <= local_bit_pos);
            return local_bit_pos + (leaf_pos * LeafTypeBitWidth);
        }
        return -1;
    }

    uint32_t ConcurrentBinaryTreeU32::NumLeaf() const
    {
        return packed_leaf_count_ * LeafTypeBitWidth;
    }

    // テストコード.
    void ConcurrentBinaryTreeU32::Test()
    {
        ConcurrentBinaryTreeU32 cbt;
        cbt.Initialize(513);
        
        cbt.SetBit(0, 1);
        cbt.SetBit(1, 1);
        cbt.SetBit(3, 1);
        cbt.SetBit(513, 1);
        assert(1 == cbt.GetBit(0));
        assert(1 == cbt.GetBit(1));
        assert(0 == cbt.GetBit(2));
        assert(1 == cbt.GetBit(3));
        assert(0 == cbt.GetBit(4));
        assert(1 == cbt.GetBit(513));
        cbt.SumReduction();
        assert(4 == cbt.GetSum());

        
        // i番目の1の位置.
        const auto bit1_location_0 = cbt.Find_ith_Bit1(0);
        const auto bit1_location_1 = cbt.Find_ith_Bit1(1);
        const auto bit1_location_2 = cbt.Find_ith_Bit1(2);
        const auto bit1_location_3 = cbt.Find_ith_Bit1(3);
        const auto bit1_location_4 = cbt.Find_ith_Bit1(4);
        assert(0 == bit1_location_0);
        assert(1 == bit1_location_1);
        assert(3 == bit1_location_2);
        assert(513 == bit1_location_3);
        assert(-1 == bit1_location_4);

        // i番目の0の位置.
        const auto bit0_location_0 = cbt.Find_ith_Bit0(0);
        const auto bit0_location_1 = cbt.Find_ith_Bit0(1);
        const auto bit0_location_2 = cbt.Find_ith_Bit0(2);
        const auto bit0_location_3 = cbt.Find_ith_Bit0(3);
        const auto bit0_location_4 = cbt.Find_ith_Bit0(4);
        const auto bit0_location_5 = cbt.Find_ith_Bit0(512);
        assert(2 == bit0_location_0);
        assert(4 == bit0_location_1);
        assert(5 == bit0_location_2);
        assert(6 == bit0_location_3);
        assert(7 == bit0_location_4);
        assert(516 == bit0_location_5);
        
        cbt.Clear();

        const auto num_leaf = cbt.NumLeaf();
        for(uint32_t i = 0; i < cbt.NumLeaf(); ++i)
        {
            cbt.SetBit(i, 1);
        }
        cbt.SumReduction();
        assert(cbt.NumLeaf() == cbt.GetSum());
        
        cbt.Clear();
    }

}  // namespace ngl::render::app

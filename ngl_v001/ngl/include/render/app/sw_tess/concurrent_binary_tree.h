/*
    concurrent_binary_tree.h
    完全二分木ビットフィールドクラスの定義
*/

#pragma once

#include "util/bit_operation.h"
#include <vector>
#include <cstdint>

namespace ngl::render::app
{
    // 完全二分木ビットフィールド（32bit uint リーフ特化）
    class ConcurrentBinaryTreeU32
    {
        using LeafType = uint32_t;
        static constexpr uint32_t LeafTypeBitWidth = sizeof(LeafType) * 8;
        static constexpr uint32_t LeafTypePackedIndexShift = ngl::MostSignificantBit32(LeafTypeBitWidth);
        static constexpr uint32_t LeafTypeBitLocalIndexMask = (1u << LeafTypePackedIndexShift) - 1;
        static constexpr uint32_t SumValueLocation = 1;

    public:
        ConcurrentBinaryTreeU32()  = default;
        ~ConcurrentBinaryTreeU32() = default;

        void Initialize(uint32_t require_leaf_count);
        void Clear();

        void SetBit(uint32_t index, uint32_t bit);
        uint32_t GetBit(uint32_t index) const;

        uint32_t GetSum() const;
        void SumReduction();

        // 下位から i番目 の 1 の位置を検索. SumReduction後に使用可能.
        int Find_ith_Bit1(uint32_t i);
        // 下位から i番目 の 0 の位置を検索. SumReduction後に使用可能.
        int Find_ith_Bit0(uint32_t i);

        uint32_t NumLeaf() const;
    
        // テスト関数
        static void Test();

    private:
        std::vector<uint32_t> cbt_node_{};
        uint32_t packed_leaf_count_{};
        uint32_t packed_leaf_offset_{};
    };

}  // namespace ngl::render::app

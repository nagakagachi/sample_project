#ifndef _NAGA_BIT_OPERATION_H_
#define _NAGA_BIT_OPERATION_H_

/*
	ビット演算関連
*/

#include "util/types.h"


//#define NGL_LSB_MODE

namespace ngl
{
	// ビット1の個数
	constexpr s32 Count8bit(u8 v) {
		u8 count = (v & 0x55) + ((v >> 1) & 0x55);
		count = (count & 0x33) + ((count >> 2) & 0x33);
		return (count & 0x0f) + ((count >> 4) & 0x0f);
	}

	constexpr s32 Count16bit(u16 v) {
		u16 count = (v & 0x5555) + ((v >> 1) & 0x5555);
		count = (count & 0x3333) + ((count >> 2) & 0x3333);
		count = (count & 0x0f0f) + ((count >> 4) & 0x0f0f);
		return (count & 0x00ff) + ((count >> 8) & 0x00ff);
	}

	constexpr s32 Count32bit(u32 v) {
		u32 count = (v & 0x55555555) + ((v >> 1) & 0x55555555);
		count = (count & 0x33333333) + ((count >> 2) & 0x33333333);
		count = (count & 0x0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f);
		count = (count & 0x00ff00ff) + ((count >> 8) & 0x00ff00ff);
		return (count & 0x0000ffff) + ((count >> 16) & 0x0000ffff);
	}

	constexpr s32 Count64bit(u64 v) {
		u64 count = (v & 0x5555555555555555) + ((v >> 1) & 0x5555555555555555);
		count = (count & 0x3333333333333333) + ((count >> 2) & 0x3333333333333333);
		count = (count & 0x0f0f0f0f0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f0f0f0f0f);
		count = (count & 0x00ff00ff00ff00ff) + ((count >> 8) & 0x00ff00ff00ff00ff);
		count = (count & 0x0000ffff0000ffff) + ((count >> 16) & 0x0000ffff0000ffff);
		return (int)((count & 0x00000000ffffffff) + ((count >> 32) & 0x00000000ffffffff));
	}

	inline constexpr s32 CountbitAutoType(u8 v){return Count8bit(v);}
	inline constexpr s32 CountbitAutoType(u16 v){return Count16bit(v);}
	inline constexpr s32 CountbitAutoType(u32 v){return Count32bit(v);}
	inline constexpr s32 CountbitAutoType(u64 v){return Count64bit(v);}


	// 最上位ビットの桁数取得
	constexpr s32 MostSignificantBit64(u64 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		v |= (v >> 8);
		v |= (v >> 16);
		v |= (v >> 32);
		return Count64bit(v) - 1;
	}
	constexpr s32 MostSignificantBit8(u8 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		return Count8bit(v) - 1;
	}
	constexpr s32 MostSignificantBit16(u16 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		v |= (v >> 8);
		return Count16bit(v) - 1;
	}
	constexpr s32 MostSignificantBit32(u32 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		v |= (v >> 8);
		v |= (v >> 16);
		return Count32bit(v) - 1;
	}


#ifdef NGL_LSB_MODE
	// 一応こっちの方が速い
	// https://zariganitosh.hatenablog.jp/entry/20090708/1247093403

	// 最下位ビット計算用テーブル
	constexpr u32* _LeastSignificantBitTable64()
	{
		static u32 table[64];
		u64 hash = 0x03F566ED27179461UL;
		for (u32 i = 0; i < 64; i++)
		{
			table[hash >> 58] = i;
			hash <<= 1;
		}
		return table;
	}
	constexpr u32* LeastSignificantBitTable64()
	{
		static u32* table = _LeastSignificantBitTable64();
		return table;
	}


	// 最下位ビットだけを残す
	// 00110100 -> 00000100
	constexpr u64 LeastSignificantBitOnly(const u64 arg)
	{
		return arg & (~arg + 1);
	}
	
	// 最下位ビットの桁を返す
	// arg==0 の場合は -1
	constexpr s32 LeastSignificantBit64(const u64 arg)
	{
		if (arg == 0) return -1;
		u64 y = LeastSignificantBitOnly( arg );
		u32 i = (u32)((y * 0x03F566ED27179461UL) >> 58);
		return LeastSignificantBitTable64()[i];
	}
#else
	constexpr s32 LeastSignificantBit8(u8 v)
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		return 8 - Count8bit(v);
	}
	constexpr s32 LeastSignificantBit16(u16 v)
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		v |= (v << 8);
		return 16 - Count16bit(v);
	}
	constexpr s32 LeastSignificantBit32(u32 v) 
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		v |= (v << 8);
		v |= (v << 16);
		return 32 - Count32bit(v);
	}
	constexpr s32 LeastSignificantBit64(u64 v)
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		v |= (v << 8);
		v |= (v << 16);
		v |= (v << 32);
		return 64 - Count64bit(v);
	}
#endif



    //	与えられたビット列を2bit飛ばしに変換
    //	0111 -> 0001001001
    static constexpr u32 BitSeparate2(u32 v)
    {
#if 1
        // https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
#else
        v = (v | v << 16) & 0xff0000ff;	// 11111111000000000000000011111111
        v = (v | v << 8) & 0x0f00f00f;	// 00001111000000001111000000001111
        v = (v | v << 4) & 0xc30c30c3;	// 11000011000011000011000011000011
        v = (v | v << 2) & 0x49249249;
        return v;
#endif
    }
    //	与えられたビット列を2bitずつ詰めて返す
    //	0111 -> 0001001001
    static constexpr u32 BitCompact2(u32 v)
    {
        v = v &					0x49249249u;
        v = (v ^ (v >> 2)) &	0xC30C30C3u;
        v = (v ^ (v >> 4)) &	0x0F00F00Fu;
        v = (v ^ (v >> 8)) &	0xFF0000FFu;
        v = (v ^ (v >> 16)) &	0x0000FFFFu;

        return v;
    }

    //	与えられたビット列を2bit飛ばしに変換(64bit)
    //	0111 -> 0001001001
    static constexpr u64 BitSeparate2_u64(u64 v) 
    {
        v = (v | v << 32ull) & 0b1111111111111111000000000000000000000000000000001111111111111111ull;
        v = (v | v << 16ull) &	0b0000000011111111000000000000000011111111000000000000000011111111ull;
        v = (v | v << 8ull) &	0b1111000000001111000000001111000000001111000000001111000000001111ull;
        v = (v | v << 4ull) &	0b0011000011000011000011000011000011000011000011000011000011000011ull;
        v = (v | v << 2ull) &	0b1001001001001001001001001001001001001001001001001001001001001001ull;
        return v;
    }
    static_assert(BitSeparate2_u64(0b1000000000000000000000ull) == 0b1000000000000000000000000000000000000000000000000000000000000000ull);
    static_assert(BitSeparate2_u64(0b1000000000000000000011ull) == 0b1000000000000000000000000000000000000000000000000000000000001001ull);
    static_assert(BitSeparate2_u64(0b1011000000000000000011ull) == 0b1000001001000000000000000000000000000000000000000000000000001001ull);

    //	与えられたビット列を2bitずつ詰めて返す(64bit)
    //	0111 -> 0001001001
    static constexpr u64 BitCompact2_u64(u64 v)
    {
        v = v &						0x9249249249249249ull;// 001001001 でマスク.
        v = (v ^ (v >> 2ull)) &		0x30C30C30C30C30C3ull;
        v = (v ^ (v >> 4ull)) &		0xF00F00F00F00F00Full;
        v = (v ^ (v >> 8ull)) &		0x00FF0000FF0000FFull;
        v = (v ^ (v >> 16ull)) &	0xFFFF00000000FFFFull;
        v = (v ^ (v >> 32ull)) &	0x00000000FFFFFFFFull;
        return v;
    }
    static_assert(0b1110000100000000000100ull == BitCompact2_u64(BitSeparate2_u64(0b1110000100000000000100ull)));
    static_assert(0b0110000100000000000111ull == BitCompact2_u64(BitSeparate2_u64(0b0110000100000000000111ull)));
    static_assert(0x2fffffull == BitCompact2_u64(BitSeparate2_u64(0x2fffffull)));


    // in : [0 , 1023]
    // 3Dセル座標から符号付き32bitモートンコード計算. 入力は各軸10bitまで.
    // 入力の範囲は0から1023 (10bit).
    // 使用bitwidthは 30bit=10*3 .
    // Calculates a 32-bit Morton code from [0 : 1023] range.
    // https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
    template<bool SAFETY_MODE = false>
    static constexpr u32 EncodeMortonCodeX10Y10Z10(int x, int y, int z)
    {
        if constexpr (SAFETY_MODE)
        {
            constexpr int range = (0x01 << 10) - 1;
            x = std::min(std::max(x, 0), range);//クランプ
            y = std::min(std::max(y, 0), range);//クランプ
            z = std::min(std::max(z, 0), range);//クランプ
        }
        u32 xx = BitSeparate2((u32)x);
        u32 yy = BitSeparate2((u32)y);
        u32 zz = BitSeparate2((u32)z);
        return xx | (yy << 1) | (zz << 2);
    }
    // 32bitモートンコードからセル座標復元
    static constexpr void DecodeMortonCodeX10Y10Z10(u32 morton, int& x, int& y, int& z)
    {
        constexpr int range = (0x01 << 10) - 1;
        x = BitCompact2(morton) & range;
        y = BitCompact2(morton >> 1) & range;
        z = BitCompact2(morton >> 2) & range;
    }

    // 3Dセル座標から符号付き64bitモートンコード計算. 入力は各軸21bitまで.
    // 使用bitwidthは 63bit=21*3 .
    template<bool SAFETY_MODE = false>
    static constexpr u64 EncodeMortonCodeX21Y21Z21(int x, int y, int z)
    {
        if constexpr (SAFETY_MODE)
        {
            constexpr int range = (0x01 << 21) - 1;
            x = std::min(std::max(x, 0), range);//クランプ
            y = std::min(std::max(y, 0), range);//クランプ
            z = std::min(std::max(z, 0), range);//クランプ
        }
        u64 xx = BitSeparate2_u64((u64)x);
        u64 yy = BitSeparate2_u64((u64)y);
        u64 zz = BitSeparate2_u64((u64)z);
        return xx | (yy << 1) | (zz << 2);
    }
    // モートンコードからセル座標復元
    static constexpr void DecodeMortonCodeX21Y21Z21(u64 morton, int& x, int& y, int& z)
    {
        constexpr int range = (0x01 << 21) - 1;
        x = BitCompact2_u64(morton) & range;
        y = BitCompact2_u64(morton >> 1) & range;
        z = BitCompact2_u64(morton >> 2) & range;
    }




}


#endif
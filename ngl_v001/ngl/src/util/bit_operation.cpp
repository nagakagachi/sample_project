
#include "util/bit_operation.h"

namespace ngl
{
	// ビット1の個数
	s32 Count8bit(u8 v) {
		u8 count = (v & 0x55) + ((v >> 1) & 0x55);
		count = (count & 0x33) + ((count >> 2) & 0x33);
		return (count & 0x0f) + ((count >> 4) & 0x0f);
	}

	s32 Count16bit(u16 v) {
		u16 count = (v & 0x5555) + ((v >> 1) & 0x5555);
		count = (count & 0x3333) + ((count >> 2) & 0x3333);
		count = (count & 0x0f0f) + ((count >> 4) & 0x0f0f);
		return (count & 0x00ff) + ((count >> 8) & 0x00ff);
	}

	s32 Count32bit(u32 v) {
		u32 count = (v & 0x55555555) + ((v >> 1) & 0x55555555);
		count = (count & 0x33333333) + ((count >> 2) & 0x33333333);
		count = (count & 0x0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f);
		count = (count & 0x00ff00ff) + ((count >> 8) & 0x00ff00ff);
		return (count & 0x0000ffff) + ((count >> 16) & 0x0000ffff);
	}

	s32 Count64bit(u64 v) {
		u64 count = (v & 0x5555555555555555) + ((v >> 1) & 0x5555555555555555);
		count = (count & 0x3333333333333333) + ((count >> 2) & 0x3333333333333333);
		count = (count & 0x0f0f0f0f0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f0f0f0f0f);
		count = (count & 0x00ff00ff00ff00ff) + ((count >> 8) & 0x00ff00ff00ff00ff);
		count = (count & 0x0000ffff0000ffff) + ((count >> 16) & 0x0000ffff0000ffff);
		return (int)((count & 0x00000000ffffffff) + ((count >> 32) & 0x00000000ffffffff));
	}

	// 最上位ビットの桁数取得
	s32 MostSignificantBit64(u64 v)
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
	s32 MostSignificantBit8(u8 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		return Count8bit(v) - 1;
	}
	s32 MostSignificantBit16(u16 v)
	{
		if (v == 0) return -1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		v |= (v >> 8);
		return Count16bit(v) - 1;
	}
	s32 MostSignificantBit32(u32 v)
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
	u32* _LeastSignificantBitTable64()
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
	u32* LeastSignificantBitTable64()
	{
		static u32* table = _LeastSignificantBitTable64();
		return table;
	}

	// 最下位ビットの桁を返す
	// arg==0 の場合は -1
	s32 LeastSignificantBit64(const u64 arg)
	{
		if (arg == 0) return -1;
		u64 y = LeastSignificantBitOnly( arg );
		u32 i = (u32)((y * 0x03F566ED27179461UL) >> 58);
		return LeastSignificantBitTable64()[i];
	}
#else
	s32 LeastSignificantBit8(u8 v)
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		return 8 - Count8bit(v);
	}
	s32 LeastSignificantBit16(u16 v)
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		v |= (v << 8);
		return 16 - Count16bit(v);
	}
	s32 LeastSignificantBit32(u32 v) 
	{
		if (v == 0) return -1;
		v |= (v << 1);
		v |= (v << 2);
		v |= (v << 4);
		v |= (v << 8);
		v |= (v << 16);
		return 32 - Count32bit(v);
	}
	s32 LeastSignificantBit64(u64 v)
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


	// 最下位ビットだけを残す
	// 00110100 -> 00000100
	u64 LeastSignificantBitOnly(const u64 arg)
	{
		return arg & (~arg + 1);
	}
}
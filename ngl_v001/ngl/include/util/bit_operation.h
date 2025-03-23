#ifndef _NAGA_BIT_OPERATION_H_
#define _NAGA_BIT_OPERATION_H_

/*
	ビット演算関連
*/

#include "util/types.h"


#define NGL_LSB_MODE

namespace ngl
{
	// ビットの値が1の個数 8bit
	s32 Count8bit(u8 v);

	// ビットの値が1の個数 16bit
	s32 Count16bit(u16 v);

	// ビットの値が1の個数 32bit
	s32 Count32bit(u32 v);

	// ビットの値が1の個数 64bit
	s32 Count64bit(u64 v);

	inline s32 CountbitAutoType(u8 v){return Count8bit(v);}
	inline s32 CountbitAutoType(u16 v){return Count16bit(v);}
	inline s32 CountbitAutoType(u32 v){return Count32bit(v);}
	inline s32 CountbitAutoType(u64 v){return Count64bit(v);}


	// 最上位ビットの桁を返す
	// arg==0 の場合は 負数
	s32 MostSignificantBit8(u8 v);
	s32 MostSignificantBit16(u16 v);
	s32 MostSignificantBit32(u32 v);
	s32 MostSignificantBit64(u64 v);


#ifdef NGL_LSB_MODE
	// 最下位ビット計算用テーブル
	u32* _LeastSignificantBitTable64();
	u32* LeastSignificantBitTable64();
	// 最下位ビットの桁を返す
	// arg==0 の場合は 負数
	s32 LeastSignificantBit64(const u64 arg);
#else
	s32 LeastSignificantBit8(u8 v);
	s32 LeastSignificantBit16(u16 v);
	s32 LeastSignificantBit32(u32 v);
	s32 LeastSignificantBit64(u64 v);
#endif

	// 最下位ビットだけを残す
	// 00110100 -> 00000100
	u64 LeastSignificantBitOnly(const u64 arg);


}


#endif
#if 0
    bit_util.hlsli
#endif

#ifndef NGL_SHADER_BIT_UTIL_HLSLI
#define NGL_SHADER_BIT_UTIL_HLSLI

// ビット操作関連のユーティリティ関数


    // ビット数え上げ
    // 32bit version.
    int BitCount(uint v)
    {
        uint count = (v & 0x55555555) + ((v >> 1) & 0x55555555);
        count = (count & 0x33333333) + ((count >> 2) & 0x33333333);
        count = (count & 0x0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f);
        count = (count & 0x00ff00ff) + ((count >> 8) & 0x00ff00ff);
        return (count & 0x0000ffff) + ((count >> 16) & 0x0000ffff);
    }
    // 最大ビット位置
    // 32bit version.
    int Msb(uint v)
    {
        if (v == 0)
            return -1; // ゼロの場合は-1

        v |= (v >> 1);
        v |= (v >> 2);
        v |= (v >> 4);
        v |= (v >> 8);
        v |= (v >> 16);
        return BitCount(v) - 1;
    }
    // 最小ビット位置
    // 32bit version.
    int Lsb(uint v)
    {
        if (v == 0)
            return -1; // ゼロの場合は-1

        v |= (v << 1);
        v |= (v << 2);
        v |= (v << 4);
        v |= (v << 8);
        v |= (v << 16);
        return 32 - BitCount(v);
    }


    //	与えられたビット列を2bit飛ばしに変換
    //	0111 -> 0001001001
    uint BitSeparate2(uint v)
    {
        // https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }
    //	与えられたビット列を2bitずつ詰めて返す
    //	0111 -> 0001001001
    uint BitCompact2(uint v)
    {
        v = v &					0x49249249u;
        v = (v ^ (v >> 2)) &	0xC30C30C3u;
        v = (v ^ (v >> 4)) &	0x0F00F00Fu;
        v = (v ^ (v >> 8)) &	0xFF0000FFu;
        v = (v ^ (v >> 16)) &	0x0000FFFFu;
        return v;
    }

    // in : [0 , 1023]
    // 3Dセル座標から符号付き32bitモートンコード計算. 入力は各軸10bitまで.
    // 入力の範囲は0から1023 (10bit).
    // 使用bitwidthは 30bit=10*3 .
    // Calculates a 32-bit Morton code from [0 : 1023] range.
    // https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
    uint EncodeMortonCodeX10Y10Z10(int3 v)
    {
        #if 0
            const int range = (0x01 << 10) - 1;
            v.x = clamp(v.x, 0, range);//クランプ
            v.y = clamp(v.y, 0, range);//クランプ
            v.z = clamp(v.z, 0, range);//クランプ
        #endif
        const uint xx = BitSeparate2(v.x);
        const uint yy = BitSeparate2(v.y);
        const uint zz = BitSeparate2(v.z);
        return xx | (yy << 1) | (zz << 2);
    }
    #define k_10_bit_mask (0x3FF) // 10bitマスク
    // 32bitモートンコードからセル座標復元
    int3 DecodeMortonCodeX10Y10Z10(uint morton)
    {
        int3 result;
        result.x = BitCompact2(morton) &  k_10_bit_mask;
        result.y = BitCompact2(morton >> 1) & k_10_bit_mask;
        result.z = BitCompact2(morton >> 2) & k_10_bit_mask;
        return result;
    }

#endif
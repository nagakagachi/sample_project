/*
    cbt_tess_init_leaf.hlsl
    CBTリーフノードの初期化専用コンピュートシェーダー
    HalfEdge数分のビットを1にセット。Sum Reductionは別シェーダーで実行！
*/

#include "cbt_tess_common.hlsli"

[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint thread_id = id.x;
    
    // リーフノードの初期化のみ実装
    uint leaf_count = GetCBTLeafCount();
    uint leaf_offset = GetCBTLeafOffset();
    
    if (thread_id < leaf_count)
    {
        uint leaf_index = leaf_offset + thread_id;
        uint start_bit = thread_id * 32;
        uint end_bit = min(start_bit + 32, total_half_edges);
        
        uint bit_value = 0;
        // total_half_edges範囲内のビットのみを1に設定
        for (uint i = start_bit; i < end_bit; ++i)
        {
            bit_value |= (1u << (i - start_bit));
        }

        // デバッグコード
        #if 0
            bit_value = 0;
            if(0 == thread_id)
            {
                bit_value = 1;
            }
            if(1 == thread_id)
            {
                bit_value = 1 << 1;
            }
            if(2 == thread_id)
            {
                bit_value = (1 << 1) | (1);
            }
            if(3 == thread_id)
            {
                bit_value = (1 << 2) | (1);
            }
            if((256/32)-1 == thread_id)
            {
                bit_value = (1 << 31);
            }
            bit_value = ~bit_value;

        #endif

        
        cbt_buffer_rw[leaf_index] = bit_value;
    }
}


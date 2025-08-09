// cbt_tess_cache_index.hlsl
#include "cbt_tess_common.hlsli"

// バイセクタのインデックスをキャッシュに格納するパス
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
    
    
    //if(!tessellation_update) return;


    // CBTルート値から有効なBisector数を取得
    uint active_bisector_count = GetCBTRootValue(cbt_buffer);
    uint available_slots = bisector_pool_max_size - active_bisector_count;
    // 範囲チェック: 有効Bisector総数と利用可能スロット数の大きい方
    uint max_range = max(active_bisector_count, available_slots);
    if (thread_id < max_range)
    {
        int2 cache_value = int2(-1, -1);  // 初期値: 無効インデックス
        
        // x: i番目の1ビット（使用中Bisector）のインデックス
        int bit_index_1 = FindIthBit1InCBT(cbt_buffer, thread_id);
        cache_value.x = bit_index_1;
        
        // y: i番目の0ビット（未使用Bisector）のインデックス
        int bit_index_0 = FindIthBit0InCBT(cbt_buffer, thread_id);
        cache_value.y = bit_index_0;
        
        // インデックスキャッシュに書き込み
        index_cache_rw[thread_id] = cache_value;
    }
}

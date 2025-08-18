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
        if(active_bisector_count > thread_id)
        {
            index_cache_rw[thread_id] = FindIthBit1InCBT(cbt_buffer, thread_id);
        }

        if(available_slots > thread_id)
        {
            index_cache_rw[bisector_pool_max_size-1 - thread_id] = FindIthBit0InCBT(cbt_buffer, thread_id);
        }
    }
}

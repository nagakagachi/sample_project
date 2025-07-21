// cbt_tess_update_cbt_bitfield.hlsl
#include "cbt_tess_common.hlsli"

// CBTのビットフィールドを更新するパス
// 分割/統合されたBisectorのCBTビット状態を更新
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
    
    // CBTルート値から有効なBisector数を取得
    uint active_bisector_count = GetCBTRootValue(cbt_buffer);
    
    // 有効なBisector範囲外は早期リターン
    if (thread_id >= active_bisector_count) return;
    
    // index_cacheから有効なBisectorインデックスを取得
    int bisector_index = index_cache[thread_id].x;  // x = i番目の1ビット（使用中Bisector）
    
    uint command = bisector_pool[bisector_index].command;
    
    // commandが0の場合は何もしない（自身のビットは1のまま維持）
    if (command == 0) return;
    
    // 分割や統合が行われたBisector
    
    // 1. alloc_ptrに格納された新規Bisectorインデックスのビットを1にする
    for (uint i = 0; i < 4; ++i)
    {
        int new_bisector_index = bisector_pool[bisector_index].alloc_ptr[i];
        if (new_bisector_index >= 0)  // 負数は無効なインデックス
        {
            SetCBTBit(cbt_buffer_rw, new_bisector_index, 1);
        }
    }
    
    // 2. 自身のインデックスのビットを0にする（無効化）
    SetCBTBit(cbt_buffer_rw, bisector_index, 0);
}


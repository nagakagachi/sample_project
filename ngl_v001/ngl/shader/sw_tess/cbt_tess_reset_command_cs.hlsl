// cbt_tess_reset_command.hlsl
#include "cbt_tess_common.hlsli"

// コマンドバッファをリセットするパス
// 有効なBisectorのcommandフィールドをゼロリセット
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;


    if(!tessellation_update) return;


    // 有効なBisector範囲外は早期リターン
    if (thread_id >= GetCBTRootValue(cbt_buffer)) return;
    // index_cacheから有効なBisectorインデックスを取得
    int bisector_index = index_cache[thread_id].x;  // x = i番目の1ビット（使用中Bisector）
    

        // デバッグ機能
        bisector_pool_rw[bisector_index].debug_value = 0;


    
    // Bisectorのcommandフィールドをゼロリセット
    bisector_pool_rw[bisector_index].command = 0;

    // デバッグ用更新フラグによるスキップで不整合が起きないように念の為クリア.
    for (uint i = 0; i < BISECTOR_ALLOC_PTR_SIZE; ++i)
    {
        bisector_pool_rw[bisector_index].alloc_ptr[i] = -1;  // 無効値で初期化
    }


}


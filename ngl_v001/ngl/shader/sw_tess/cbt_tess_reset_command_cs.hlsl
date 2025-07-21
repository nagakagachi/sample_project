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
    
    // CBTルート値から有効なBisector数を取得
    uint active_bisector_count = GetCBTRootValue(cbt_buffer);
    
    // 有効なBisector範囲外は早期リターン
    if (thread_id >= active_bisector_count) return;
    
    // index_cacheから有効なBisectorインデックスを取得
    int bisector_index = index_cache[thread_id].x;  // x = i番目の1ビット（使用中Bisector）
    
    // Bisectorのcommandフィールドをゼロリセット
    bisector_pool_rw[bisector_index].command = 111;
}


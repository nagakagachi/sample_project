// cbt_tess_begin_update.hlsl
/*
    CBT適応的テッセレーション フレーム開始処理
    
    処理内容:
    - alloc_counter を0クリア（新規Bisector割り当てカウンタリセット）
    - CBTルートノード値によるIndirect Dispatch引数更新
    - Draw Indirect引数の初期化
*/

#include "cbt_tess_common.hlsli"

// CBT適応的テッセレーション開始処理
[numthreads(1, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    // スレッド0のみで処理
    if (DTid.x != 0) return;
    
    // alloc_counterを0クリア（新規Bisector割り当てカウンタリセット）
    alloc_counter_rw[0] = 0;
    
    // CBTルート値（現在の有効Bisector数）を取得
    uint active_bisector_count = GetCBTRootValue(cbt_buffer);
    uint total_bisector_pool_size = bisector_pool_max_size;
    uint unused_bisector_count = total_bisector_pool_size - active_bisector_count;
    
    // Bisector処理用Indirect Dispatch引数を設定
    // 有効なBisector数に基づいてDispatch
    uint bisector_thread_groups = (active_bisector_count + CBT_THREAD_GROUP_SIZE - 1) / CBT_THREAD_GROUP_SIZE;
    indirect_dispatch_arg_for_bisector[0] = bisector_thread_groups;
    indirect_dispatch_arg_for_bisector[1] = 1;  // Y軸スレッド数は1
    indirect_dispatch_arg_for_bisector[2] = 1;  // Z
    
    // Index Cache更新用Indirect Dispatch引数を設定
    // 有効Bisectorと未使用Bisectorの多い方に基づいてDispatch
    uint index_cache_work_size = max(active_bisector_count, unused_bisector_count);
    uint index_cache_thread_groups = (index_cache_work_size + CBT_THREAD_GROUP_SIZE - 1) / CBT_THREAD_GROUP_SIZE;
    indirect_dispatch_arg_for_index_cache[0] = index_cache_thread_groups;
    indirect_dispatch_arg_for_index_cache[1] = 1;
    indirect_dispatch_arg_for_index_cache[2] = 1;

    // Draw Indirect引数を初期化
    // 有効なBisector数 × 3（三角形の頂点数）で描画
    draw_indirect_arg[0] = active_bisector_count * 3;// VertexCountPerInstance: 各Bisectorが1つの三角形
    draw_indirect_arg[1] = 1;                          // InstanceCount: インスタンス数は1
    draw_indirect_arg[2] = 0;                          // StartVertexLocation: 開始頂点位置
    draw_indirect_arg[3] = 0;                          // StartInstanceLocation: 開始インスタンス位置
}

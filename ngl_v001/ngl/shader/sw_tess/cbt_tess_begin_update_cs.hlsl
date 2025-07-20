// cbt_tess_begin_update.hlsl
/*
    CBT適応的テッセレーション フレーム開始処理（初期実装版）
    
    処理内容:
    - alloc_counter を0クリア（新規Bisector割り当てカウンタリセット）
    
    今後追加予定:
    - CBTルートノード値によるIndirect Dispatch引数更新
    - index_cache更新用引数設定
*/

#include "cbt_tess_common.hlsli"

// CBT適応的テッセレーション開始処理（初期実装）
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
}

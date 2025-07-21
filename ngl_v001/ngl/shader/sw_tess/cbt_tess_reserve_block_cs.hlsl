// cbt_tess_reserve_block.hlsl
#include "cbt_tess_common.hlsli"

// メモリブロックを予約するパス
// generate_commandで生成されたコマンドに基づいて新規Bisectorを予約
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
    
    // 早期リターン：スレッド範囲チェック
    if (thread_id >= GetCBTRootValue(cbt_buffer)) {
        return;
    }
    
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;
    
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool[bisector_index];
    uint command = bisector.command;
    
    // 分割コマンドのチェックと子Bisector情報の計算
    if (command & (BISECTOR_CMD_TWIN_SPLIT | BISECTOR_CMD_PREV_SPLIT | BISECTOR_CMD_NEXT_SPLIT))
    {
        // 最初の子Bisectorの情報を計算
        uint2 first_child_info = CalcFirstChildBisectorInfo(bisector.bs_id, bisector.bs_depth);
        uint first_child_id = first_child_info.x;
        uint child_depth = first_child_info.y;
        uint second_child_id = first_child_id + 1;  // 二番目の子は +1
        
        // TODO: 実際のBisector割り当て処理
        // 1. alloc_counterをアトミック増分して新規インデックス取得
        // 2. 子Bisectorを新規インデックスに作成
        // 3. alloc_ptrに割り当て先インデックスを保存
        
        // 例：分割ロジック（実装予定）
        // uint new_bisector_index1, new_bisector_index2;
        // if (ReserveBisectorSlots(2, new_bisector_index1, new_bisector_index2))
        // {
        //     InitializeChildBisector(new_bisector_index1, first_child_id, child_depth, bisector);
        //     InitializeChildBisector(new_bisector_index2, second_child_id, child_depth, bisector);
        //     bisector_pool_rw[bisector_index].alloc_ptr[0] = new_bisector_index1;
        //     bisector_pool_rw[bisector_index].alloc_ptr[1] = new_bisector_index2;
        // }
    }
    // 分割コマンドが無い場合のみ統合処理を実行
    else if (command & (BISECTOR_CMD_BOUNDARY_MERGE | BISECTOR_CMD_INTERIOR_MERGE))
    {
        // 親Bisectorの情報を計算
        uint2 parent_info = CalcParentBisectorInfo(bisector.bs_id, bisector.bs_depth);
        uint parent_id = parent_info.x;
        uint parent_depth = parent_info.y;
        
        // TODO: 実際の統合処理
        // 1. 兄弟Bisectorとの統合判定
        // 2. 統合代表・同意ビットのチェック
        // 3. 親Bisectorの作成または既存親への統合
        
        // 例：統合ロジック（実装予定）
        // if (command & BISECTOR_CMD_MERGE_REPRESENTATIVE)
        // {
        //     uint new_parent_index;
        //     if (ReserveBisectorSlots(1, new_parent_index))
        //     {
        //         InitializeParentBisector(new_parent_index, parent_id, parent_depth, bisector);
        //         bisector_pool_rw[bisector_index].alloc_ptr[0] = new_parent_index;
        //     }
        // }
    }
}


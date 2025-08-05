// cbt_tess_fill_new_block.hlsl
#include "cbt_tess_common.hlsli"

// 新しいブロックを生成して初期化するパス
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;

    // 有効なBisector範囲外は早期リターン
    if (thread_id >= GetCBTRootValue(cbt_buffer)) return;
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;


    if(!tessellation_update) return;

    
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool_rw[bisector_index];
    uint command = bisector.command;
    
    // 分割コマンドがある場合の分割処理
    if (command & BISECTOR_CMD_ANY_SPLIT)
    {
        // Twin分割のみ対応.
        if ((command & BISECTOR_CMD_TWIN_SPLIT))
        {
            // 予約された子Bisectorのインデックスを取得
            uint first_child_index = bisector.alloc_ptr[0];
            uint second_child_index = bisector.alloc_ptr[1];
            
            // 子Bisectorの基本情報を計算
            uint2 child_info = CalcFirstChildBisectorInfo(bisector.bs_id, bisector.bs_depth);
            uint first_child_id = child_info.x;
            uint child_depth = child_info.y;
            uint second_child_id = first_child_id + 1;
            
            // 最初の子Bisectorを初期化
            ResetBisector(bisector_pool_rw[first_child_index], first_child_id, child_depth);
            bisector_pool_rw[first_child_index].command = 0;  // コマンドは次フレームで設定
            
            // 最初の子のリンク設定
            bisector_pool_rw[first_child_index].twin = bisector.prev;  // 親のprev
            bisector_pool_rw[first_child_index].next = second_child_index;  // 親の2つ目の子
            
            // 親にtwinがいれば親のtwinの2つ目の子、いなければ無効値
            if (bisector.twin >= 0)
            {
                Bisector parent_twin = bisector_pool_rw[bisector.twin];
                bisector_pool_rw[first_child_index].prev = parent_twin.alloc_ptr[1];  // 親のtwinの2つ目の子
            }
            else
            {
                bisector_pool_rw[first_child_index].prev = -1;  // 無効値
            }
            
            // 2つ目の子Bisectorを初期化
            ResetBisector(bisector_pool_rw[second_child_index], second_child_id, child_depth);
            bisector_pool_rw[second_child_index].command = 0;  // コマンドは次フレームで設定
            
            // 2つ目の子のリンク設定
            bisector_pool_rw[second_child_index].twin = bisector.next;  // 親のnext
            bisector_pool_rw[second_child_index].prev = first_child_index;  // 親の最初の子
            
            // 親にTwinがいれば親のTwinの最初の子、いなければ無効値
            if (bisector.twin >= 0)
            {
                Bisector parent_twin = bisector_pool_rw[bisector.twin];
                bisector_pool_rw[second_child_index].next = parent_twin.alloc_ptr[0];  // 親のtwinの最初の子
            }
            else
            {
                bisector_pool_rw[second_child_index].next = -1;  // 無効値
            }
        }
    }
    // 統合処理
    else if ((command & (BISECTOR_CMD_BOUNDARY_MERGE | BISECTOR_CMD_INTERIOR_MERGE)) &&
             (command & BISECTOR_CMD_MERGE_CONSENT))
    {
        // 統合代表の場合のみ処理
        if (command & BISECTOR_CMD_MERGE_REPRESENTATIVE)
        {
            // 境界統合の処理
            if (command & BISECTOR_CMD_BOUNDARY_MERGE)
            {
                // 予約された親Bisectorのインデックスを取得
                uint parent_index = bisector.alloc_ptr[0];
                
                // 統合相手を取得
                int merge_partner_index = bisector.next;
                Bisector merge_partner = bisector_pool_rw[merge_partner_index];
                
                // 親Bisectorの基本情報を計算
                uint2 parent_info = CalcParentBisectorInfo(bisector.bs_id, bisector.bs_depth);
                uint parent_id = parent_info.x;
                uint parent_depth = parent_info.y;
                
                // 親Bisectorを初期化
                ResetBisector(bisector_pool_rw[parent_index], parent_id, parent_depth);
                bisector_pool_rw[parent_index].command = 0;  // コマンドは次フレームで設定
                
                // 親のリンク設定（分割の逆）
                bisector_pool_rw[parent_index].next = merge_partner.twin;
                bisector_pool_rw[parent_index].prev = bisector.twin;
                bisector_pool_rw[parent_index].twin = bisector.prev;
            }
            // 内部統合の処理
            else if (command & BISECTOR_CMD_INTERIOR_MERGE)
            {
                // 予約された親Bisectorのインデックスを取得
                uint first_parent_index = bisector.alloc_ptr[0];
                uint second_parent_index = bisector.alloc_ptr[1];
                
                // 統合する4つのBisectorを取得
                Bisector bj1 = bisector;  // 統合代表
                int bj2_index = bj1.next;
                Bisector bj2 = bisector_pool_rw[bj2_index];
                int bj3_index = bj2.next;
                Bisector bj3 = bisector_pool_rw[bj3_index];
                int bj4_index = bj3.next;
                Bisector bj4 = bisector_pool_rw[bj4_index];
                
                // 第1親Bisectorの基本情報を計算
                uint2 first_parent_info = CalcParentBisectorInfo(bj1.bs_id, bj1.bs_depth);
                uint first_parent_id = first_parent_info.x;
                uint parent_depth = first_parent_info.y;
                
                // 第2親Bisectorの基本情報を計算
                uint2 second_parent_info = CalcParentBisectorInfo(bj3.bs_id, bj3.bs_depth);
                uint second_parent_id = second_parent_info.x;
                
                // 第1親Bisectorを初期化
                ResetBisector(bisector_pool_rw[first_parent_index], first_parent_id, parent_depth);
                bisector_pool_rw[first_parent_index].command = 0;  // コマンドは次フレームで設定
                
                // 第2親Bisectorを初期化
                ResetBisector(bisector_pool_rw[second_parent_index], second_parent_id, parent_depth);
                bisector_pool_rw[second_parent_index].command = 0;  // コマンドは次フレームで設定
                
                // 第1親のリンク設定（分割の逆）
                bisector_pool_rw[first_parent_index].next = bj2.twin;
                bisector_pool_rw[first_parent_index].prev = bj1.twin;
                bisector_pool_rw[first_parent_index].twin = second_parent_index;//bj1.prev;
                
                // 第2親のリンク設定（分割の逆）
                bisector_pool_rw[second_parent_index].next = bj4.twin;
                bisector_pool_rw[second_parent_index].prev = bj3.twin;
                bisector_pool_rw[second_parent_index].twin = first_parent_index;//bj3.prev;
            }
        }
    }
}


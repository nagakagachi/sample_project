// cbt_tess_update_neighbor.hlsl
#include "cbt_tess_common.hlsli"

// 分割による隣接Bisectorのリンク更新（RefinePointers実装）
void RefinePointers(uint bisector_index)
{
    // 分割で生成された子Bisectorのインデックスを取得
    uint first_child_index = bisector_pool_rw[bisector_index].alloc_ptr[0];
    uint second_child_index = bisector_pool_rw[bisector_index].alloc_ptr[1];
    
    int next_index = bisector_pool_rw[bisector_index].next;
    int prev_index = bisector_pool_rw[bisector_index].prev;
    
    if (next_index >= 0)
    {
        if (bisector_pool_rw[next_index].command & BISECTOR_CMD_TWIN_SPLIT)
        {
            // 隣接でTwin分割されている場合.
            int target_next_index = bisector_pool_rw[next_index].alloc_ptr[0];  // 第1子

            bisector_pool_rw[target_next_index].twin = second_child_index;
        }
        else
        {
            if (bisector_pool_rw[next_index].prev == (int)bisector_index)
            {
                bisector_pool_rw[next_index].prev = second_child_index;
            }
            else
            {
                bisector_pool_rw[next_index].twin = second_child_index;
            }
        }
        
    }
    
    if (prev_index >= 0)
    {
        if (bisector_pool_rw[prev_index].command & BISECTOR_CMD_TWIN_SPLIT)
        {
            // 隣接でTwin分割されている場合.
            int target_prev_index = bisector_pool_rw[prev_index].alloc_ptr[1];  // 第2子
            
            bisector_pool_rw[target_prev_index].twin = first_child_index;
        }
        else
        {
            if (bisector_pool_rw[prev_index].next == (int)bisector_index)
            {
                bisector_pool_rw[prev_index].next = first_child_index;
            }
            else
            {
                bisector_pool_rw[prev_index].twin = first_child_index;
            }
        }
    }
}

// DecimatePointers実装：統合される2つのBisectorのペアを一つに統合
void DecimatePointers(uint first_child_index, uint second_child_index, uint parent_index)
{
    int next_index = bisector_pool_rw[second_child_index].twin;
    int prev_index = bisector_pool_rw[first_child_index].twin;
    
    if (next_index >= 0)
    {
        int edit_neighbor_index = next_index;
        // 現実装では隣接で統合のみが許可され, 分割の隣接では統合は発生しないためそのパターンは考慮しない.
        if (bisector_pool_rw[next_index].command & BISECTOR_CMD_MERGE_CONSENT)
        {
            // reserve時にINTERIORの場合は2つ目のペアの1つ目(prev)の確保ポインタには0番目にそのペアが使用するポインタを設定しているため.
            // BOUNDARYでもINTERIORでも同じように, ペアの1つ目の確保ポインタの0番目を使用できるように簡略化.
            edit_neighbor_index = (0==(bisector_pool_rw[next_index].bs_id & 0x01))? bisector_pool_rw[next_index].alloc_ptr[0] : bisector_pool_rw[bisector_pool_rw[next_index].prev].alloc_ptr[0];
        }

        if (bisector_pool_rw[edit_neighbor_index].prev == (int)second_child_index)
            bisector_pool_rw[edit_neighbor_index].prev = parent_index;
        else
            bisector_pool_rw[edit_neighbor_index].twin = parent_index;
    }
    
    if (prev_index >= 0)
    {
        int edit_neighbor_index = prev_index;
        // 現実装では隣接で統合のみが許可され, 分割の隣接では統合は発生しないためそのパターンは考慮しない.
        if (bisector_pool_rw[prev_index].command & BISECTOR_CMD_MERGE_CONSENT)
        {
            // reserve時にINTERIORの場合は2つ目のペアの1つ目(prev)の確保ポインタには0番目にそのペアが使用するポインタを設定しているため.
            // BOUNDARYでもINTERIORでも同じように, ペアの1つ目の確保ポインタの0番目を使用できるように簡略化.
            edit_neighbor_index = (0==(bisector_pool_rw[prev_index].bs_id & 0x01))? bisector_pool_rw[prev_index].alloc_ptr[0] : bisector_pool_rw[bisector_pool_rw[prev_index].prev].alloc_ptr[0];
        }

        if (bisector_pool_rw[edit_neighbor_index].next == (int)first_child_index)
            bisector_pool_rw[edit_neighbor_index].next = parent_index;
        else
            bisector_pool_rw[edit_neighbor_index].twin = parent_index;
    }
}

// 統合による隣接Bisectorのリンク更新
void UpdateNeighborsForMerge(uint bisector_index)
{
    Bisector bisector = bisector_pool_rw[bisector_index];
    uint command = bisector.command;
    
    // 境界統合の場合：1つのペアを統合
    if (command & BISECTOR_CMD_BOUNDARY_MERGE)
    {
        // 統合される2つのBisector（b^(d+1)_(2j), b^(d+1)_(2j+1)）
        uint first_child_index = bisector_index;  // b^(d+1)_(2j)（統合代表）
        int second_child_index = bisector.next;   // b^(d+1)_(2j+1)（統合相手）
        
        // 統合で生成された親Bisector（b^d_j）
        uint parent_index = bisector.alloc_ptr[0];        
        DecimatePointers(first_child_index, second_child_index, parent_index);
    }
    // 内部統合の場合：2つのペアを統合（DecimatePointersを2回呼び出し）
    else if (command & BISECTOR_CMD_INTERIOR_MERGE)
    {
        // 統合される4つのBisector
        Bisector bj1 = bisector_pool_rw[bisector_index];          // 第1ペアの代表
        Bisector bj2 = bisector_pool_rw[bj1.next];                // 第1ペアの相手
        Bisector bj3 = bisector_pool_rw[bj2.next];                // 第2ペアの代表
        Bisector bj4 = bisector_pool_rw[bj3.next];                // 第2ペアの相手
        
        // 統合で生成された2つの親Bisector
        uint first_parent_index = bisector.alloc_ptr[0];       // 第1親（bj1, bj2の統合結果）
        uint second_parent_index = bisector.alloc_ptr[1];      // 第2親（bj3, bj4の統合結果）
        
        // 第1ペア（bj1, bj2）にDecimatePointersを適用
        DecimatePointers(bisector_index, bj1.next, first_parent_index);
        
        // 第2ペア（bj3, bj4）にDecimatePointersを適用
        DecimatePointers(bj2.next, bj3.next, second_parent_index);
    }
    
    // TODO: 将来の拡張
    // 隣接Bisectorでの統合がサポートされた場合、ここに追加処理を実装
}

// 隣接情報を更新するパス
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
    
    // 有効なBisector範囲外は早期リターン
    if (thread_id >= GetCBTRootValue(cbt_buffer)) return;
    
    


    // デバッグ
    if(0 != debug_mode_int)
    {
        return;
    }


    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;
    
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool_rw[bisector_index];
    uint command = bisector.command;
    
    // 分割コマンドがある場合の隣接更新処理
    if (command & BISECTOR_CMD_ANY_SPLIT)
    {
        // 分割による隣接Bisectorのリンク更新（RefinePointers）
        if (command & BISECTOR_CMD_TWIN_SPLIT)
        {
            RefinePointers(bisector_index);
        }
    }
    // 統合コマンドがある場合の隣接更新処理
    else if ((command & (BISECTOR_CMD_BOUNDARY_MERGE | BISECTOR_CMD_INTERIOR_MERGE)) &&
             (command & BISECTOR_CMD_MERGE_REPRESENTATIVE) &&
             (command & BISECTOR_CMD_MERGE_CONSENT))
    {
        // 統合代表かつ統合同意がある場合のみ隣接Bisectorのリンク更新
        UpdateNeighborsForMerge(bisector_index);
    }
}


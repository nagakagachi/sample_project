// cbt_tess_update_neighbor.hlsl
#include "cbt_tess_common.hlsli"

// 分割による隣接Bisectorのリンク更新（RefinePointers実装）
void RefinePointers(uint bisector_index)
{
    // 分割で生成された子Bisectorのインデックスを取得
    uint first_child_index = bisector_pool_rw[bisector_index].alloc_ptr[0];   // b^(d+1)_(2j)
    uint second_child_index = bisector_pool_rw[bisector_index].alloc_ptr[1];  // b^(d+1)_(2j+1)
    
    // next ← Next(b^d_j)
    int next_index = bisector_pool_rw[bisector_index].next;
    
    // prev ← Prev(b^d_j)
    int prev_index = bisector_pool_rw[bisector_index].prev;
    
    // if Prev(next) = b^d_j then
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
                // Prev(next) ← b^(d+1)_(2j+1)
                bisector_pool_rw[next_index].prev = second_child_index;
            }
            else
            {
                // Twin(next) ← b^(d+1)_(2j+1)
                bisector_pool_rw[next_index].twin = second_child_index;
            }
        }
        
    }
    
    // if Next(prev) = b^d_j then
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
                // Next(prev) ← b^(d+1)_(2j)
                bisector_pool_rw[prev_index].next = first_child_index;
            }
            else
            {
                // Twin(prev) ← b^(d+1)_(2j)
                bisector_pool_rw[prev_index].twin = first_child_index;
            }
        }
    }
}

// DecimatePointers実装：統合される2つのBisectorのペアを一つに統合
void DecimatePointers(uint first_child_index, uint second_child_index, uint parent_index)
{
    // procedure DecimatePointers(b^(d+1)_(2j), b^(d+1)_(2j+1))
    Bisector first_child = bisector_pool_rw[first_child_index];    // b^(d+1)_(2j)
    Bisector second_child = bisector_pool_rw[second_child_index];  // b^(d+1)_(2j+1)
    
    // next ← Twin(b^(d+1)_(2j+1))
    int next_index = second_child.twin;
    
    // prev ← Twin(b^(d+1)_(2j))
    int prev_index = first_child.twin;
    
    // if Prev(next) = b^(d+1)_(2j+1) then
    if (next_index >= 0)
    {
        Bisector next_bisector = bisector_pool_rw[next_index];
        if (next_bisector.prev == (int)second_child_index)
        {
            // Prev(next) ← b^d_j
            bisector_pool_rw[next_index].prev = parent_index;
        }
        else
        {
            // Twin(next) ← b^d_j
            bisector_pool_rw[next_index].twin = parent_index;
        }
    }
    
    // if Next(prev) = b^(d+1)_(2j) then
    if (prev_index >= 0)
    {
        Bisector prev_bisector = bisector_pool_rw[prev_index];
        if (prev_bisector.next == (int)first_child_index)
        {
            // Next(prev) ← b^d_j
            bisector_pool_rw[prev_index].next = parent_index;
        }
        else
        {
            // Twin(prev) ← b^d_j
            bisector_pool_rw[prev_index].twin = parent_index;
        }
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
        
        if (second_child_index >= 0)
        {
            // DecimatePointers適用
            DecimatePointers(first_child_index, second_child_index, parent_index);
        }
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
    
    // 早期リターン：スレッド範囲チェック
    if (thread_id >= GetCBTRootValue(cbt_buffer)) {
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
        //UpdateNeighborsForMerge(bisector_index);
    }
}


// cbt_tess_reserve_block.hlsl
#include "cbt_tess_common.hlsli"

// 境界統合の条件をチェックする関数
// 注意: bisector_indexは統合代表（bs_id最小）のBisectorを指す
bool CheckBoundaryMergeConditions(uint bisector_index)
{
    Bisector bj1 = bisector_pool_rw[bisector_index];
    
    // bj2を取得（統合代表から見て常にnextの兄弟）
    int bj2_index = bj1.next;
    if (bj2_index < 0) return false;
    
    Bisector bj2 = bisector_pool_rw[bj2_index];
    
    // 条件1: 両方のBisectorが分割コマンドを持たない
    if ((bj1.command & BISECTOR_CMD_ANY_SPLIT) || (bj2.command & BISECTOR_CMD_ANY_SPLIT))
    {
        return false;
    }
    
    // 条件2: bj2も境界統合コマンドを持つ（bj1は代表なので必ず持つ）
    if (!(bj2.command & BISECTOR_CMD_BOUNDARY_MERGE))
    {
        return false;
    }
    
    // 条件3: 近傍Bisectorも分割コマンドを持たない
    if (bj1.twin >= 0)
    {
        if (bisector_pool_rw[bj1.twin].command & BISECTOR_CMD_ANY_SPLIT)
            return false;
    }
    
    // bj2の近傍チェック
    if (bj2.twin >= 0)
    {
        if (bisector_pool_rw[bj2.twin].command & BISECTOR_CMD_ANY_SPLIT)
            return false;
    }
    
    return true;
}

// 内部統合の条件をチェックする関数
// 注意: bisector_indexは統合代表（bs_id最小）のBisectorを指す
bool CheckInteriorMergeConditions(uint bisector_index)
{
    Bisector bj1 = bisector_pool_rw[bisector_index];
    
    // 4つのBisector（bj1, bj2, bj3, bj4）を順次取得
    // 統合代表から見て: bj2=next, bj3=bj2.next, bj4=bj3.next
    int bj2_index = bj1.next;
    if (bj2_index < 0) return false;
    
    Bisector bj2 = bisector_pool_rw[bj2_index];
    
    int bj3_index = bj2.next;
    if (bj3_index < 0) return false;
    
    Bisector bj3 = bisector_pool_rw[bj3_index];
    
    int bj4_index = bj3.next;
    if (bj4_index < 0) return false;
    
    Bisector bj4 = bisector_pool_rw[bj4_index];
    
    // 条件1: 4つすべてのBisectorが分割コマンドを持たない
    if ((bj1.command & BISECTOR_CMD_ANY_SPLIT) || (bj2.command & BISECTOR_CMD_ANY_SPLIT) ||
        (bj3.command & BISECTOR_CMD_ANY_SPLIT) || (bj4.command & BISECTOR_CMD_ANY_SPLIT))
    {
        return false;
    }
    
    // 条件2: bj2, bj3, bj4も内部統合コマンドを持つ（bj1は代表なので必ず持つ）
    if (!(bj2.command & BISECTOR_CMD_INTERIOR_MERGE) ||
        !(bj3.command & BISECTOR_CMD_INTERIOR_MERGE) ||
        !(bj4.command & BISECTOR_CMD_INTERIOR_MERGE))
    {
        return false;
    }
    
    // 条件3: 各Bisectorの近傍も分割コマンドを持たない
    int neighbors[] = { bj1.twin, bj2.twin, bj3.twin, bj4.twin };
    
    for (int i = 0; i < 4; ++i)
    {
        if (neighbors[i] >= 0)
        {
            if (bisector_pool_rw[neighbors[i]].command & BISECTOR_CMD_ANY_SPLIT)
                return false;
        }
    }
    
    return true;
}





// メモリブロックを予約するパス
// generate_commandで生成されたコマンドに基づいて新規Bisectorを予約
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
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool_rw[bisector_index];
    uint command = bisector.command;
    
    // 分割コマンドのチェックと子Bisector情報の計算
    if (command & BISECTOR_CMD_ANY_SPLIT)
    {
        // 簡単のためTwin分割のみ実行
        if ((command & BISECTOR_CMD_TWIN_SPLIT))
        {
            // 新規アロケーション: 2つのBisectorを確保
            const int nAlloc = 2;
            int counter;
            InterlockedAdd(alloc_counter_rw[0], -nAlloc, counter);
            
            // アロケーション要求数が確保できているかチェック
            if (counter < nAlloc)
            {
                // 要求数が確保できない：カウンタを元に戻して終了
                InterlockedAdd(alloc_counter_rw[0], nAlloc);
                return;
            }
            
            // 未使用インデックスを取得（アロケーション成功時は必ず有効）
            const int first_unused_index = index_cache[counter - nAlloc].y;
            const int second_unused_index = index_cache[counter - nAlloc + 1].y;
            
            // 割り当て先インデックスをalloc_ptrに保存
            bisector_pool_rw[bisector_index].alloc_ptr[0] = first_unused_index;
            bisector_pool_rw[bisector_index].alloc_ptr[1] = second_unused_index;
        }
    }
    else if (command & (BISECTOR_CMD_BOUNDARY_MERGE | BISECTOR_CMD_INTERIOR_MERGE))
    {
        // 統合代表の場合のみ処理
        if (command & BISECTOR_CMD_MERGE_REPRESENTATIVE)
        {
            bool merge_allowed = false;
            
            // 境界統合の場合
            if (command & BISECTOR_CMD_BOUNDARY_MERGE)
            {
                merge_allowed = CheckBoundaryMergeConditions(bisector_index);
            }
            // 内部統合の場合
            else if (command & BISECTOR_CMD_INTERIOR_MERGE)
            {
                merge_allowed = CheckInteriorMergeConditions(bisector_index);
            }
            
            // 統合条件を満たす場合のみ統合処理を実行
            if (merge_allowed)
            {
                // 境界統合か内部統合かでアロケーション数を決定
                // 境界統合: 1つの親Bisector, 内部統合: 2つの親Bisector
                int nAlloc = (command & BISECTOR_CMD_BOUNDARY_MERGE) ? 1 : 2;
                
                int counter;
                InterlockedAdd(alloc_counter_rw[0], -nAlloc, counter);
                
                // アロケーション要求数が確保できているかチェック
                if (counter < nAlloc)
                {
                    // 要求数が確保できない：カウンタを元に戻して終了
                    InterlockedAdd(alloc_counter_rw[0], nAlloc);
                    return;
                }
                
                // 未使用インデックスを取得（アロケーション成功時は必ず有効）
                const int first_parent_index = index_cache[counter - nAlloc].y;
                bisector_pool_rw[bisector_index].alloc_ptr[0] = first_parent_index;
                
                // 境界統合の場合：統合対象bisectorに統合同意ビットを立てる
                if (command & BISECTOR_CMD_BOUNDARY_MERGE)
                {
                    Bisector current_bisector = bisector_pool_rw[bisector_index];
                    
                    // 統合代表（bj1）に統合同意ビットを立てる.
                    bisector_pool_rw[bisector_index].command |= BISECTOR_CMD_MERGE_CONSENT;
                    
                    // 統合相手（bj2）にも統合同意ビットを立てる
                    bisector_pool_rw[current_bisector.next].command |= BISECTOR_CMD_MERGE_CONSENT;
                }
                // 内部統合の場合：もう一方のペアの代表（bs_id最小）にも同じ情報を書き込み
                else if (command & BISECTOR_CMD_INTERIOR_MERGE)
                {
                    const int second_parent_index = index_cache[counter - nAlloc + 1].y;
                    bisector_pool_rw[bisector_index].alloc_ptr[1] = second_parent_index;
                    
                    Bisector current_bisector = bisector_pool_rw[bisector_index];
                    
                    // もう一方のペアの代表は bj1.next.next（bj3）
                    // CheckInteriorMergeConditionsで存在確認済みのためifチェック不要
                    int bj2_index = current_bisector.next;
                    Bisector bj2 = bisector_pool_rw[bj2_index];
                    int bj3_index = bj2.next;
                    int bj4_index = bisector_pool_rw[bj3_index].next;
                    
                    // 第2ペアの代表（bj3）にも同じalloc_ptr情報を書き込み. neighbor処理時に簡略化するためにこのペアが使用するsecond_parent_indexを1つ目の要素にいれる.
                    bisector_pool_rw[bj3_index].alloc_ptr[0] = second_parent_index;
                    bisector_pool_rw[bj3_index].alloc_ptr[1] = first_parent_index;
                    
                    // 4つすべてのbisectorに統合同意ビットを立てる
                    bisector_pool_rw[bisector_index].command |= BISECTOR_CMD_MERGE_CONSENT;
                    bisector_pool_rw[bj2_index].command |= BISECTOR_CMD_MERGE_CONSENT;
                    bisector_pool_rw[bj3_index].command |= BISECTOR_CMD_MERGE_CONSENT;
                    bisector_pool_rw[bj4_index].command |= BISECTOR_CMD_MERGE_CONSENT;
                }
            }
        }
    }
}


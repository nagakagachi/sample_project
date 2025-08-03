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

    if(!tessellation_update) return;
    
    // 有効なBisector範囲外は早期リターン
    if (thread_id >= GetCBTRootValue(cbt_buffer)) return;
    
    

    // index_cacheから有効なBisectorインデックスを取得
    int bisector_index = index_cache[thread_id].x;  // x = i番目の1ビット（使用中Bisector）
    
    uint command = bisector_pool[bisector_index].command;
    
    // commandが0の場合は何もしない（自身のビットは1のまま維持）
    if (command == 0) return;
    
    // 分割コマンドの処理
    if (command & BISECTOR_CMD_ANY_SPLIT)
    {
        // 1. alloc_ptrに格納された新規Bisectorインデックスのビットを1にする
        for (uint i = 0; i < BISECTOR_ALLOC_PTR_SIZE; ++i)
        {
            int new_bisector_index = bisector_pool[bisector_index].alloc_ptr[i];
            if (new_bisector_index >= 0)  // 負数は無効なインデックス
            {
                SetCBTBit(cbt_buffer_rw, new_bisector_index, 1);
            }
        }
        
        if(0 <= bisector_pool[bisector_index].alloc_ptr[0])
        {
            // 分割の子が適切に確保できていた場合のみ自身を無効化. コマンドが設定されていてもアロケーション失敗している可能性があるため).
            // 必要分確保できていなければすべて -1 なので0番目のみチェック.
            SetCBTBit(cbt_buffer_rw, bisector_index, 0);
        }
    }
    // 統合コマンドの処理（代表かつ同意ビットが立っている場合のみ）
    else if ((command & (BISECTOR_CMD_BOUNDARY_MERGE | BISECTOR_CMD_INTERIOR_MERGE)) &&
             (command & BISECTOR_CMD_MERGE_REPRESENTATIVE) &&
             (command & BISECTOR_CMD_MERGE_CONSENT))
    {
        // 1. alloc_ptrに格納された新規Bisectorインデックスのビットを1にする
        for (uint i = 0; i < BISECTOR_ALLOC_PTR_SIZE; ++i)
        {
            int new_bisector_index = bisector_pool[bisector_index].alloc_ptr[i];
            if (new_bisector_index >= 0)  // 負数は無効なインデックス
            {
                SetCBTBit(cbt_buffer_rw, new_bisector_index, 1);
            }
        }
        
        // 2. 統合対象のBisectorインデックスのビットを0にする
        if(0 <= bisector_pool[bisector_index].alloc_ptr[0])
        {
            // 子が適切に確保できていた場合のみ自身を無効化. コマンドが設定されていてもアロケーション失敗している可能性があるため).
            // 必要分確保できていなければすべて -1 なので0番目のみチェック.

            if (command & BISECTOR_CMD_BOUNDARY_MERGE)
            {
                // 境界統合：自身と統合相手のビットを0にする
                int merge_partner_index = bisector_pool[bisector_index].next;
                SetCBTBit(cbt_buffer_rw, bisector_index, 0);
                SetCBTBit(cbt_buffer_rw, merge_partner_index, 0);
            }
            else if (command & BISECTOR_CMD_INTERIOR_MERGE)
            {
                // 内部統合：4つのBisectorのビットを0にする
                Bisector bj1 = bisector_pool[bisector_index];
                Bisector bj2 = bisector_pool[bj1.next];
                Bisector bj3 = bisector_pool[bj2.next];
                
                SetCBTBit(cbt_buffer_rw, bisector_index, 0);        // bj1
                SetCBTBit(cbt_buffer_rw, bj1.next, 0);             // bj2
                SetCBTBit(cbt_buffer_rw, bj2.next, 0);             // bj3
                SetCBTBit(cbt_buffer_rw, bj3.next, 0);             // bj4
            }
        }
    }
}


// cbt_tess_generate_command_cs.hlsl
#include "cbt_tess_common.hlsli"

// 分割時に必要な最大アロケーションBisector数を計算する関数
uint CalcMaxAllocationForSplit(uint depth)
{
    return 3 * depth + 4;
}

// 統合時に必要な最大アロケーションBisector数を計算する関数
uint CalcMaxAllocationForMerge()
{
    return 2;
}

// 実際の分割コマンドビットを書き込む関数
void WriteSplitCommands(uint bisector_index)
{   
    // 新実装：Twinチェーン追跡による分割許可判定
    const uint MAX_CHAIN_LENGTH = 16;
    uint bisector_chain[MAX_CHAIN_LENGTH];
    uint chain_length = 0;
    
    uint current_index = bisector_index;
    bool split_allowed = false;
    
    // Twinリンクを辿ってチェーンを構築
    while (chain_length < MAX_CHAIN_LENGTH)
    {
        // 現在のBisectorをチェーンに追加
        bisector_chain[chain_length++] = current_index;
        
        // 現在のBisectorのTwinを取得
        Bisector current_bisector = bisector_pool_rw[current_index];
        int twin_index = current_bisector.twin;
        
        // 終了条件チェック
        if (twin_index < 0)
        {
            // Twinが無効（境界エッジ）→ 分割許可
            split_allowed = true;
            break;
        }
        
        // Twin のTwinをチェック
        Bisector twin_bisector = bisector_pool_rw[twin_index];
        if (twin_bisector.twin == (int)current_index)
        {
            // Twin ペアが成立（twin.twin == self）→ 分割許可
            // Twin もチェーンに追加
            if (chain_length < MAX_CHAIN_LENGTH)
            {
                bisector_chain[chain_length++] = (uint)twin_index;
            }
            split_allowed = true;
            break;
        }
        
        // 次のBisectorに進む
        current_index = (uint)twin_index;
    }
    
    // 分割許可判定と実行
    if (split_allowed)
    {
        // チェーン内の全BisectorにTwin分割コマンドを設定
        for (uint i = 0; i < chain_length; ++i)
        {
            InterlockedOr(bisector_pool_rw[bisector_chain[i]].command, BISECTOR_CMD_TWIN_SPLIT);
        }
        
        // チェーン上の隣接要素間の関係性に応じた分割コマンドを追加設定
        for (uint i = 0; i < chain_length - 1; ++i)
        {
            uint current_bisector_index = bisector_chain[i];
            uint next_bisector_index = bisector_chain[i + 1];
            
            // next_bisector から見て current_bisector との関係を判定
            Bisector next_bisector = bisector_pool_rw[next_bisector_index];
            
            if (next_bisector.twin == (int)current_bisector_index)
            {
                // current が next のTwinの場合
                InterlockedOr(bisector_pool_rw[next_bisector_index].command, BISECTOR_CMD_TWIN_SPLIT);
            }
            else if (next_bisector.prev == (int)current_bisector_index)
            {
                // current が next のPrevの場合
                InterlockedOr(bisector_pool_rw[next_bisector_index].command, BISECTOR_CMD_PREV_SPLIT);
            }
            else if (next_bisector.next == (int)current_bisector_index)
            {
                // current が next のNextの場合
                InterlockedOr(bisector_pool_rw[next_bisector_index].command, BISECTOR_CMD_NEXT_SPLIT);
            }
        }
    }
    // else: 配列サイズ不足または無限ループ検出 → 分割キャンセル（何もしない）
    
    // 追加のPrev/Next分割判定も可能
    // 必要に応じてPrev/Next Bisectorに対しても分割コマンドを設定
    // 例: bisector_pool_rw[bj.prev_index].command |= BISECTOR_CMD_PREV_SPLIT;
    //     bisector_pool_rw[bj.next_index].command |= BISECTOR_CMD_NEXT_SPLIT;
}

// 実際の統合コマンドビットを書き込む関数
void WriteMergeCommands(uint bisector_index)
{
    // bj1: 統合対象のBisector
    Bisector bj1 = bisector_pool_rw[bisector_index];
    uint j1 = bj1.bs_id;
    uint depth_j1 = bj1.bs_depth;
    
    // minimum_tree_depth以下の場合は統合不可（早期リターン）
    if (depth_j1 <= cbt_mesh_minimum_tree_depth) return;
    
    // bitValue ← BitwiseAnd(j1, 1)
    uint bit_value = j1 & 1;
    
    // bj2 ← bitValue ? Prev(bj1) : Next(bj1)
    // bj3 ← bitValue ? Next(bj1) : Prev(bj1)
    int bj2_index = bit_value ? bj1.prev : bj1.next;
    int bj3_index = bit_value ? bj1.next : bj1.prev;
    
    // bj2が有効かチェック
    if (bj2_index < 0) return;
    
    Bisector bj2 = bisector_pool_rw[bj2_index];
    uint j2 = bj2.bs_id;
    
    // if ⌊j1/2⌋ = ⌊j2/2⌋ then (兄弟Bisectorかチェック)
    if ((j1 >> 1) == (j2 >> 1))
    {
        // if bj3 = null then (境界統合)
        if (bj3_index < 0)
        {
            // Merge(bj1, bj2) - boundary
            // 境界統合：処理対象のbisectorのみコマンドを書き込み
            uint command = BISECTOR_CMD_BOUNDARY_MERGE;
            
            // bs_idが最小の場合は代表ビットも設定
            if (j1 < j2)
            {
                command |= BISECTOR_CMD_MERGE_REPRESENTATIVE;
            }
            
            InterlockedOr(bisector_pool_rw[bisector_index].command, command);
        }
        // else if depth_j1 = depth_j3 then
        else 
        {
            Bisector bj3 = bisector_pool_rw[bj3_index];
            uint depth_j3 = bj3.bs_depth;
            
            if (depth_j1 == depth_j3)
            {
                uint j3 = bj3.bs_id;
                
                // bj4 ← bitValue ? Next(bj3) : Prev(bj3)
                int bj4_index = bit_value ? bj3.next : bj3.prev;
                
                if (bj4_index >= 0)
                {
                    Bisector bj4 = bisector_pool_rw[bj4_index];
                    uint j4 = bj4.bs_id;
                    
                    // if ⌊j3/2⌋ = ⌊j4/2⌋ then
                    if ((j3 >> 1) == (j4 >> 1))
                    {
                        // Merge(bj1, bj2, bj3, bj4) - non-boundary
                        // 非境界統合：処理対象のbisectorのみコマンドを書き込み
                        uint min_bs_id = min(min(j1, j2), min(j3, j4));
                        uint command = BISECTOR_CMD_INTERIOR_MERGE;
                        
                        // bs_idが最小の場合は代表ビットも設定
                        if (j1 == min_bs_id)
                        {
                            command |= BISECTOR_CMD_MERGE_REPRESENTATIVE;
                        }
                        
                        InterlockedOr(bisector_pool_rw[bisector_index].command, command);
                    }
                }
            }
        }
    }
}

// 分割コマンドを設定する関数（隣接Bisectorのコマンドも含めて直接書き換え）
void SetSplitCommands(uint bisector_index)
{
    // Bisector情報を取得
    Bisector bisector = bisector_pool_rw[bisector_index];
    
    // 分割に必要な最大アロケーション数を計算
    uint max_allocation = CalcMaxAllocationForSplit(bisector.bs_depth);
    
    // アロケーションカウンタを原子的に増加
    uint old_counter;
    InterlockedAdd(alloc_counter_rw[0], max_allocation, old_counter);
    
    // CBTのルート値（現在の使用中Bisector数）を取得
    uint current_used = GetCBTRootValue(cbt_buffer);
    
    // アロケーション可能性をチェック（現在使用数 + 新規割り当て <= 最大プールサイズ）
    if (current_used + old_counter + max_allocation <= bisector_pool_max_size)
    {
        // アロケーション可能：実際の分割コマンドを書き込み
        WriteSplitCommands(bisector_index);
    }
    else
    {
        // アロケーション不可：カウンタを元に戻して終了
        InterlockedAdd(alloc_counter_rw[0], -(int)max_allocation);
    }
}

// 統合コマンドを設定する関数（隣接Bisectorのコマンドも含めて直接書き換え）
void SetMergeCommands(uint bisector_index)
{
    // 統合に必要な最大アロケーション数を計算
    uint max_allocation = CalcMaxAllocationForMerge();
    
    // アロケーションカウンタを原子的に増加
    uint old_counter;
    InterlockedAdd(alloc_counter_rw[0], max_allocation, old_counter);
    
    // CBTのルート値（現在の使用中Bisector数）を取得
    uint current_used = GetCBTRootValue(cbt_buffer);
    
    // アロケーション可能性をチェック（現在使用数 + 新規割り当て <= 最大プールサイズ）
    if (current_used + old_counter + max_allocation <= bisector_pool_max_size)
    {
        // アロケーション可能：実際の統合コマンドを書き込み
        WriteMergeCommands(bisector_index);
    }
    else
    {
        // アロケーション不可：カウンタを元に戻して終了
        InterlockedAdd(alloc_counter_rw[0], -(int)max_allocation);
    }
}




// Bisectorのコマンドを生成するパス
// 有効なBisectorごとに分割・統合判定を行い、commandフィールドを更新する
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
    
    // デバッグコード：特定の場合だけ実行
    //if (240 != GetCBTRootValue(cbt_buffer)) {
    //    return;
    //}
    
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;
    
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool_rw[bisector_index];
    
    // Bisectorの基本頂点インデックスを取得 (curr, next, prev)
    int3 base_vertex_indices = CalcRootBisectorBaseVertex(bisector.bs_id, bisector.bs_depth);
    
    // Bisectorの頂点属性補間マトリックスを取得
    float3x3 attribute_matrix = CalcBisectorAttributeMatrix(bisector.bs_id, bisector.bs_depth);
    
    // 基本三角形の頂点座標を取得
    float3 v0_base = vertex_position_buffer[base_vertex_indices.x]; // curr
    float3 v1_base = vertex_position_buffer[base_vertex_indices.y]; // next  
    float3 v2_base = vertex_position_buffer[base_vertex_indices.z]; // prev
    
    // 属性マトリックスを使ってBisectorの頂点座標を計算
    float3x3 base_positions = float3x3(v0_base, v1_base, v2_base);
    float3x3 bisector_positions = mul(attribute_matrix, base_positions);
    
    // Bisectorの三角形頂点座標
    float3 v0 = bisector_positions[0]; // 第1頂点
    float3 v1 = bisector_positions[1]; // 第2頂点  
    float3 v2 = bisector_positions[2]; // 第3頂点
    
    // ワールド空間に変換
    float3 v0_world = mul(object_to_world, float4(v0, 1.0f)).xyz;
    float3 v1_world = mul(object_to_world, float4(v1, 1.0f)).xyz;
    float3 v2_world = mul(object_to_world, float4(v2, 1.0f)).xyz;
    
    // オブジェクト空間での重要座標を計算
    float3 important_point_object = mul(world_to_object, float4(important_point, 1.0f)).xyz;
    
    // Bisectorの分割・統合評価
    // 1. 三角形の重心座標を計算
    float3 triangle_center = (v0 + v1 + v2) / 3.0f;
    
    // 2. 三角形の面積を計算（外積の半分）
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float area = length(cross(edge1, edge2)) * 0.5f;
    
    // 3. important_pointからの距離を計算
    float distance_to_important = length(triangle_center - important_point_object);
    
    // 4. 分割評価値を計算（面積を距離で重み付け）
    float subdivision_value = area / max(distance_to_important, 0.001f); // ゼロ除算防止
    
    // 5. 統合閾値を動的計算（分割閾値 × 統合係数）
    float merge_threshold = tessellation_split_threshold * tessellation_merge_factor;

    bool do_subdivision = false;
    bool do_merge = false;
    if(0 <= fixed_subdivision_level)
    {
        const int effective_depth = bisector.bs_depth - cbt_mesh_minimum_tree_depth;
        // 固定分割レベルが設定されている場合
        if (effective_depth < fixed_subdivision_level)
        {
            do_subdivision = true;
            do_merge = false; // 統合は行わない
        }
        else if (effective_depth > fixed_subdivision_level)
        {
            do_subdivision = false; // 分割は行わない
            do_merge = true; // 統合は行う
        }
    }
    else
    {
        if (subdivision_value > tessellation_split_threshold)
        {
            do_subdivision = true;
            do_merge = false;
        }
        else if (subdivision_value < merge_threshold)
        {
            do_merge = true;
            do_subdivision = false;
        }
    }

    if (do_subdivision)
    {
        // 分割処理：分割コマンドを設定
        SetSplitCommands(bisector_index);
    }
    else if (do_merge)
    {
        // 統合処理：統合コマンドを設定
        SetMergeCommands(bisector_index);
    }


    // デバッグ用: 分割評価値をBisectorに保存
    bisector_pool_rw[bisector_index].debug_subdivision_value = subdivision_value;
    


    // else: 閾値範囲内の場合は何もしない（既存のcommandを保持）
}

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

// Twinチェイン全体に対して分割コマンドを書き込むバージョン. 不具合とアロケーションチェックが過剰でアロケーション失敗によるキャンセルが懸念される点から現在は未採用.
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
        int twin_index = bisector_pool_rw[current_index].twin;
        
        // 終了条件チェック
        if (twin_index < 0)
        {
            // Twinが無効（境界エッジ）→ 分割許可
            split_allowed = true;
            break;
        }
        
        // Twin のTwinをチェック
        if (bisector_pool_rw[twin_index].twin == (int)current_index)
        {
            // Twin ペアが成立（twin.twin == self）→ 分割許可
            if (chain_length < MAX_CHAIN_LENGTH)
            {
                bisector_chain[chain_length++] = (uint)twin_index;
                split_allowed = true;
            }
            break;
        }
        
        // 次のBisectorに進む
        current_index = (uint)twin_index;
    }
    
    // 分割許可判定と実行
    if (split_allowed)
    {
        // 以前の実装ではTwin分割に制限しているにも関わらず, 同一Depth(相互twin)のチェックをしていなかったためT-Junctionが発生していたため対策.
        
        for (uint i = 0; i < chain_length; ++i)
        {
            const uint current_bisector_index = bisector_chain[i];
            
            if (0 > bisector_pool_rw[current_bisector_index].twin)
            {
                // Twinが境界の場合は分割.
                InterlockedOr(bisector_pool_rw[current_bisector_index].command, BISECTOR_CMD_TWIN_SPLIT);
            }
            else if (i < chain_length-1)
            {
                const uint next_bisector_index = bisector_chain[i + 1];

                if(bisector_pool_rw[next_bisector_index].twin == (int)current_bisector_index)
                {
                    // Twinペアの両方に分割コマンドを設定.
                    InterlockedOr(bisector_pool_rw[current_bisector_index].command, BISECTOR_CMD_TWIN_SPLIT);
                    InterlockedOr(bisector_pool_rw[next_bisector_index].command, BISECTOR_CMD_TWIN_SPLIT);
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

    // CBTのルート値（現在の使用中Bisector数）を取得
    int current_used = GetCBTRootValue(cbt_buffer);
    
    #if 1
        // より単純実装：Bisector毎にTwin境界かTwinペアに行き当たるまで探索し, 1つまたはペアである2つのBisectorのみを分割する. 
        // フレーム分散で最終的に全体が目的の分割レベルに到達する. 
        // 1Fでの分割では目的の分割レベルに到達しにくいが, 代わりにロジックがシンプルになり, アロケーションチェックがBisector毎に最大2つ=新しい4つのBisectorで済みアロケーション失敗による分割停止の可能性が抑制できる.

        // Twin連鎖を辿って末端を分割.
        int current_index = bisector_index;
        int terminate_twin_index = -1;
        while (true)
        {
            // 現在のBisectorのTwinを取得
            int twin_index = bisector_pool_rw[current_index].twin;
            
            // Twinが境界のBisectorに到達したら終了.
            if (twin_index < 0)
                break;
            
            // Twin のTwinをチェック
            if (bisector_pool_rw[twin_index].twin == current_index)
            {
                terminate_twin_index = twin_index;
                break;
            }
            
            // 次のBisectorに進む
            current_index = twin_index;
        }

        // アロケーション試行.
        const int alloc_count = (0 <= terminate_twin_index) ? 4 : 2;
        int old_counter;
        InterlockedAdd(alloc_counter_rw[0], alloc_count, old_counter);

        // アロケーション可能性をチェック(現在使用数 + 新規割り当て <= 最大プールサイズ)
        if (current_used + old_counter + alloc_count <= bisector_pool_max_size)
        {
            InterlockedOr(bisector_pool_rw[current_index].command, BISECTOR_CMD_TWIN_SPLIT);
            if(0 <= terminate_twin_index)
            {
                // Twinがいる場合はその分も込でAllocチェックしているので相手にもコマンド書き込み.
                InterlockedOr(bisector_pool_rw[terminate_twin_index].command, BISECTOR_CMD_TWIN_SPLIT);
            }
        }
        else
        {
            // アロケーション不可：カウンタを元に戻して終了
            InterlockedAdd(alloc_counter_rw[0], -alloc_count);
        }

    #else
        // 元実装. 再帰でTwin隣接を巡回してコマンド書き込みしていくバージョン. 多段階分割時にT-Junctionが発生する不具合あり.

        // 分割に必要な最大アロケーション数を計算
        int max_allocation = CalcMaxAllocationForSplit(bisector.bs_depth);
        
        // アロケーションカウンタを原子的に増加
        int old_counter;
        InterlockedAdd(alloc_counter_rw[0], max_allocation, old_counter);
        
        // アロケーション可能性をチェック（現在使用数 + 新規割り当て <= 最大プールサイズ）
        if (current_used + old_counter + max_allocation <= bisector_pool_max_size)
        {
            // アロケーション可能：実際の分割コマンドを書き込み
            WriteSplitCommands(bisector_index);
        }
        else
        {
            // アロケーション不可：カウンタを元に戻して終了
            InterlockedAdd(alloc_counter_rw[0], -max_allocation);
        }
    #endif
}


// 統合コマンドの計算.
uint CalcMergeCommands(uint bisector_index)
{
    // bj1: 統合対象のBisector
    Bisector bj1 = bisector_pool_rw[bisector_index];
    uint j1 = bj1.bs_id;
    uint depth_j1 = bj1.bs_depth;

    // minimum_tree_depth以下の場合は統合不可（早期リターン）
    if (depth_j1 <= cbt_mesh_minimum_tree_depth)
        return 0;
    
    uint bit_value = j1 & 1;
    int bj2_index = bit_value ? bj1.prev : bj1.next;
    int bj3_index = bit_value ? bj1.next : bj1.prev;
    
    // bj2が有効かチェック
    if (bj2_index < 0)
        return 0;
    
    uint j2 = bisector_pool_rw[bj2_index].bs_id;

    uint out_command = 0;

    if ((j1 >> 1) == (j2 >> 1))
    {
        // if bj3 = null then (境界統合)
        if (bj3_index < 0)
        {
            // 境界統合：処理対象のbisectorのみコマンドを書き込み
            out_command = BISECTOR_CMD_BOUNDARY_MERGE;
            
            // bs_idが最小の場合は代表ビット設定
            if (j1 < j2)
            {
                out_command |= BISECTOR_CMD_MERGE_REPRESENTATIVE;
            }
        }
        else 
        {
            Bisector bj3 = bisector_pool_rw[bj3_index];
            uint depth_j3 = bj3.bs_depth;
            
            if (depth_j1 == depth_j3)
            {
                uint j3 = bj3.bs_id;
                int bj4_index = bit_value ? bj3.next : bj3.prev;
                
                if (bj4_index >= 0)
                {
                    Bisector bj4 = bisector_pool_rw[bj4_index];
                    uint j4 = bj4.bs_id;
                    
                    if ((j3 >> 1) == (j4 >> 1))
                    {
                        // 非境界統合：処理対象のbisectorのみコマンドを書き込み
                        out_command = BISECTOR_CMD_INTERIOR_MERGE;
                        
                        // bs_idが最小の場合は代表ビット設定
                        uint min_bs_id = min(min(j1, j2), min(j3, j4));
                        if (j1 == min_bs_id)
                        {
                            out_command |= BISECTOR_CMD_MERGE_REPRESENTATIVE;
                        }
                    }
                }
            }
        }
    }
    return out_command;
}

// 統合コマンドを設定する関数（隣接Bisectorのコマンドも含めて直接書き換え）
void SetMergeCommands(uint bisector_index)
{   
    // CBTのルート値（現在の使用中Bisector数）を取得
    int current_used = GetCBTRootValue(cbt_buffer);
    
    uint merge_command = CalcMergeCommands(bisector_index);
    if(0 != merge_command)
    {

        // 統合は代表が必要分の確保チェックのみする.
        // 代表以外はチェックせずにコマンド書き込みするが, 後段では代表ビットが立っているBisectorがいなければ無効になるため問題ない.
        // これによって不要なアロケーションカウンタ増加によってアロケーション失敗する統合が減る.
        if(BISECTOR_CMD_MERGE_REPRESENTATIVE & merge_command)
        {
            // 代表ビットが立っている場合はマージのモード毎に 1 か 2 のアロケーションを行う.
            const int alloc_count = (BISECTOR_CMD_BOUNDARY_MERGE & merge_command) ? 1 : 2;
            // アロケーションカウンタを原子的に増加
            int old_counter;
            InterlockedAdd(alloc_counter_rw[0], alloc_count, old_counter);

            // アロケーション可能性をチェック（現在使用数 + 新規割り当て <= 最大プールサイズ）
            if (current_used + old_counter + alloc_count <= bisector_pool_max_size)
            {
                // アロケーション成功なら 代表Bisectorビットの書き込み.
                InterlockedOr(bisector_pool_rw[bisector_index].command, merge_command);
            }
            else
            {
                // アロケーション失敗なら カウンタを元に戻して終了
                InterlockedAdd(alloc_counter_rw[0], -alloc_count);
            }
        }
        else
        {
            // 代表ビットがなければそのまま書き込み.
            InterlockedOr(bisector_pool_rw[bisector_index].command, merge_command);
        }
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

    // 有効なBisector範囲外は早期リターン
    if (thread_id >= GetCBTRootValue(cbt_buffer)) return;
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id];
    

        // デバッグ機能
        if(0 <= debug_target_bisector_id || 0 <= debug_target_bisector_depth)
        {
            // デバッグ対象のBisectorは選択されたフラグを立てる
            if(debug_target_bisector_id == bisector_pool_rw[bisector_index].bs_id ||  debug_target_bisector_depth == bisector_pool_rw[bisector_index].bs_depth)
            {
                if(0 == tessellation_debug_flag)
                {
                    if(0<= bisector_pool_rw[bisector_index].twin)
                        bisector_pool_rw[ bisector_pool_rw[bisector_index].twin ].debug_value = 1;
                }
                else if(1 == tessellation_debug_flag)
                {
                    if(0<= bisector_pool_rw[bisector_index].prev)
                        bisector_pool_rw[ bisector_pool_rw[bisector_index].prev ].debug_value = 1;
                }
                else if(2 == tessellation_debug_flag)
                {
                    if(0<= bisector_pool_rw[bisector_index].next)
                        bisector_pool_rw[ bisector_pool_rw[bisector_index].next ].debug_value = 1;
                }
            }
        }



    if(!tessellation_update) return;
    
    
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
    
    #if 1
        // ワールド空間での重要座標を計算
        // Bisectorの分割・統合評価
        // 1. 三角形の重心座標を計算
        float3 triangle_center = (v0_world + v1_world + v2_world) / 3.0f;

        // 2. 三角形の面積を計算（外積の半分）
        float3 edge1 = v1_world - v0_world;
        float3 edge2 = v2_world - v0_world;
        float area = length(cross(edge1, edge2)) * 0.5f;
        
        // 3. important_pointからの距離を計算
        //float distance_to_important = length(triangle_center - important_point);
        float distance_to_important = pow(length(triangle_center - important_point), 1.5);
    #else
        const float3 obj_axis_scale = float3(length(object_to_world._m00_m01_m02), 
                                             length(object_to_world._m10_m11_m12), 
                                             length(object_to_world._m20_m21_m22));
        const float obj_approx_scale = max(max(obj_axis_scale.x, obj_axis_scale.y), obj_axis_scale.z);

        // オブジェクト空間での重要座標を計算
        float3 important_point_object = mul(world_to_object, float4(important_point, 1.0f)).xyz;
        
        // Bisectorの分割・統合評価
        // 1. 三角形の重心座標を計算
        float3 triangle_center = (v0 + v1 + v2) / 3.0f;
        
        // 2. 三角形の面積を計算（外積の半分）
        float3 edge1 = v1 - v0;
        float3 edge2 = v2 - v0;
        float area = length(cross(edge1, edge2)) * 0.5f * obj_approx_scale;
        
        // 3. important_pointからの距離を計算
        float distance_to_important = length(triangle_center - important_point_object);
    #endif

    
    // 4. 分割評価値を計算（面積を距離で重み付け）
    float subdivision_value = area / max(distance_to_important, 0.5f); // ゼロ除算防止
    
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
        if (subdivision_value >= tessellation_split_threshold)
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
}

// cbt_tess_generate_command_cs.hlsl
#include "cbt_tess_common.hlsli"

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
    
    // index_cacheから有効なBisectorのインデックスを取得
    const uint bisector_index = index_cache[thread_id].x;
    
    // 早期リターン：有効なBisectorインデックスチェック
    if (bisector_index >= bisector_pool_max_size) {
        return;
    }
    
    // 処理対象のBisectorを取得
    Bisector bisector = bisector_pool[bisector_index];
    
    // BisectorからオリジナルRootBisectorインデックスを計算
    uint original_half_edge_index = GetRootBisectorIndex(bisector.bs_id, bisector.bs_depth);
    
    // 対応するHalfEdgeを取得
    HalfEdge half_edge = half_edge_buffer[original_half_edge_index];
    
    // 三角形の頂点座標を取得
    float3 v0 = vertex_position_buffer[half_edge.vertex];
    float3 v1 = vertex_position_buffer[half_edge_buffer[half_edge.next].vertex];
    float3 v2 = vertex_position_buffer[half_edge_buffer[half_edge.prev].vertex];
    
    // ワールド空間に変換
    float3 v0_world = mul(object_to_world, float4(v0, 1.0f)).xyz;
    float3 v1_world = mul(object_to_world, float4(v1, 1.0f)).xyz;
    float3 v2_world = mul(object_to_world, float4(v2, 1.0f)).xyz;
    
    // オブジェクト空間での重要座標を計算
    float3 important_point_object = mul(world_to_object, float4(important_point, 1.0f)).xyz;
    
    // TODO: 分割・統合判定ロジックの実装
    // 現在は雛形として情報取得のみ
    uint command = 0;

    if(v0_world.x == (1.0/0.0))
    {
        command = 1;// テスト.
    }
    
    // TODO: 以下の判定ロジックを実装する
    // 1. 三角形サイズとカメラ距離から分割判定
    // 2. Twin/Prev/Next分割の必要性判定
    // 3. 境界/非境界統合の判定
    // 4. 統合代表・同意ビット設定
    
    // commandを書き込み（現在は0で初期化）
    bisector_pool_rw[bisector_index].command = command;
}


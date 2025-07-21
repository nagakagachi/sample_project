/*
    cbt_tess_init_leaf.hlsl
    CBTリーフノードとBisectorPoolの初期化専用コンピュートシェーダー
    1. HalfEdge数分のCBTビットを1にセット
    2. 初期BisectorPoolの要素をHalfEdgeから初期化
    Sum Reductionは別シェーダーで実行！
*/

#include "cbt_tess_common.hlsli"

[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint thread_id = id.x;
    
    // 早期リターン: 有効なHalfEdge範囲外
    if (thread_id >= total_half_edges) return;
    
    // 1. CBTビット初期化（total_half_edges個のBisectorを有効化）
    SetCBTBit(cbt_buffer_rw, thread_id, 1);
    
    // 2. BisectorPool初期化（HalfEdgeから対応するBisectorを初期化）
    HalfEdge half_edge = half_edge_buffer[thread_id];
    
    // Bisectorの初期値設定
    bisector_pool_rw[thread_id].bs_depth = cbt_mesh_minimum_tree_depth;  // メッシュの最小深度
    bisector_pool_rw[thread_id].bs_id = thread_id;                       // HalfEdgeインデックスをIDとして使用
    
    // HalfEdgeのリンク情報をコピー
    bisector_pool_rw[thread_id].next = half_edge.next;
    bisector_pool_rw[thread_id].prev = half_edge.prev;
    bisector_pool_rw[thread_id].twin = half_edge.twin;
    
    // コマンドと割り当てポインタは初期化
    bisector_pool_rw[thread_id].command = 0;
    bisector_pool_rw[thread_id].alloc_ptr[0] = -1;  // 無効値
    bisector_pool_rw[thread_id].alloc_ptr[1] = -1;
    bisector_pool_rw[thread_id].alloc_ptr[2] = -1;
    bisector_pool_rw[thread_id].alloc_ptr[3] = -1;
}


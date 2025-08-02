/*
    cbt_tess_init_leaf.hlsl
    CBTリーフノードとBisectorPoolの初期化専用コンピュートシェーダ
    Bisectorプール全域に対するDispatch.
    1. 先頭からHalfEdge数分のCBTビットを1にセット
    2. それ以降の未使用要素はビットを0にセット
*/

#include "cbt_tess_common.hlsli"

[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint thread_id = id.x;
    
    if(thread_id >= bisector_pool_max_size)
    {
        return;
    }

    uint bit_value = thread_id < total_half_edges ? 1 : 0;

    // 1. CBTビット初期化（total_half_edges個のBisectorを有効化）
    SetCBTBit(cbt_buffer_rw, thread_id, bit_value);

    if (0 != bit_value)
    {
        // 2. BisectorPool初期化（HalfEdgeから対応するBisectorを初期化）
        HalfEdge half_edge = half_edge_buffer[thread_id];
        
        ResetBisector(bisector_pool_rw[thread_id], thread_id, cbt_mesh_minimum_tree_depth);

        // HalfEdgeのリンク情報をコピー
        bisector_pool_rw[thread_id].next = half_edge.next;
        bisector_pool_rw[thread_id].prev = half_edge.prev;
        bisector_pool_rw[thread_id].twin = half_edge.twin;
    }
    
}


/*
    cbt_sum_reduction_cs.hlsl
*/
#include "cbt_tess_common.hlsli"


#define CBT_SUM_REDUCTION_WIDTH_LOG2 10
#define CBT_SUM_REDUCTION_WIDTH (1 << CBT_SUM_REDUCTION_WIDTH_LOG2)

struct CbCbtSumReduction
{
    // Dispatch側で log2(CBT_SUM_REDUCTION_WIDTH)ずつDepthを減らして呼び出す.
    uint target_depth;


    uint padding0;
    uint padding1;
    uint padding2;
};
ConstantBuffer<CbCbtSumReduction> cb_sum_reduction;

// GroupSize分の総和まで計算するために L + L/2 + L/4+ ... -> 2L
groupshared uint work_mem[CBT_SUM_REDUCTION_WIDTH * 2];


[numthreads(CBT_SUM_REDUCTION_WIDTH, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint global_id = DTid.x;

    const uint local_id = GTid.x;
    const uint group_id = Gid.x;

    const int work_min_depth = min(CBT_SUM_REDUCTION_WIDTH_LOG2, int(cb_sum_reduction.target_depth));

    const int src_leaf_start = (group_id * CBT_SUM_REDUCTION_WIDTH);
    const int src_leaf_count = min(CBT_SUM_REDUCTION_WIDTH, int(1<<cb_sum_reduction.target_depth) - src_leaf_start);


    const uint depth_node_count = 1 << cb_sum_reduction.target_depth;
    const uint depth_node_offset = 1 << cb_sum_reduction.target_depth;

    const uint work_offset = 1 << work_min_depth;
    if (global_id < depth_node_count)
    {
        work_mem[work_offset + local_id] = (cb_sum_reduction.target_depth == cbt_tree_depth) ? GetCBTBit(cbt_buffer_rw, global_id) : cbt_buffer_rw[depth_node_offset + src_leaf_start + local_id];
    }
    else
    {
        work_mem[work_offset + local_id] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    // WorkMem上でSum.
    for(int d = work_min_depth-1; d >= 0; --d)
    {
        const uint store_depth_count_and_offset = 1 << d;
        if(local_id < store_depth_count_and_offset)
        {
            const uint store_index = store_depth_count_and_offset + local_id;

            // 左右の子ノードの和を計算
            work_mem[store_index] = work_mem[(store_index<<1)] + work_mem[(store_index<<1) + 1];
        }    
        GroupMemoryBarrierWithGroupSync();
    }

    // work_memからGlobalMemに書き戻し.
    int depth_count = 1;
    for(int d = work_min_depth-1; d >= 0; --d)
    {
        const uint cur_work_size = src_leaf_count >> depth_count;

        if(cur_work_size > local_id)
        {
            const uint cur_global_store_index =  ((depth_node_offset + src_leaf_start) >> depth_count) + local_id;
            cbt_buffer_rw[cur_global_store_index] = work_mem[(1 << d) + local_id];
        }
        ++depth_count;
    }
}
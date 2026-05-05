#if 0

assp_probe_build_ray_meta_cs.hlsl

ASSP probeごとのray数を算出し、group内prefix-sum + group代表atomicで
RayMeta(offset,count) と total ray count を構築する。

#endif

#include "assp_probe_common.hlsli"

groupshared uint gs_probe_ray_count[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP];
groupshared uint gs_probe_prefix_inclusive[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP];
groupshared uint gs_group_total_ray_count;
groupshared uint gs_group_global_ray_offset_base;

uint AsspComputeRayCount(uint probe_list_index, uint probe_count)
{
    if(probe_list_index >= probe_count)
    {
        return 0u;
    }

    int2 probe_id;
    if(!AsspTryGetProbeTileIdFromLinearIndex(probe_list_index, probe_id))
    {
        return 0u;
    }

    const float4 tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_id, 0));
    if(!isValidDepth(tile_info.x))
    {
        return 0u;
    }

    // 安定度スコア:
    // - variance の大きい probe は ray を増やす
    // - reprojection 失敗 / plane 差分大も ray を増やす
    const uint min_rays = min((uint)max(cb_srvs.assp_ray_budget_min_rays, 1), k_assp_ray_count_max);
    const uint max_rays = max(min((uint)max(cb_srvs.assp_ray_budget_max_rays, 1), k_assp_ray_count_max), min_rays);
    const float budget_scale = max(cb_srvs.assp_ray_budget_scale, 0.0);

    float score = 0.0;

    const float4 variance_info = AdaptiveScreenSpaceProbeHistoryVarianceTex.Load(int3(probe_id, 0));
    const float variance_signal = max(variance_info.w, 0.0);
    score += saturate(variance_signal * budget_scale) * max(cb_srvs.assp_ray_budget_variance_weight, 0.0);

    const uint best_prev_packed = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_id, 0)).x;
    if(0xffffffffu == best_prev_packed)
    {
        score += max(cb_srvs.assp_ray_budget_no_history_bias, 0.0);
    }
    else
    {
        const int2 prev_tile_id = AsspUnpackProbeTileId(best_prev_packed);
        const float4 prev_tile_info = AdaptiveScreenSpaceProbeHistoryTileInfoTex.Load(int3(prev_tile_id, 0));
        const float3 curr_n = normalize(OctDecode(tile_info.zw));
        const float3 prev_n = normalize(OctDecode(prev_tile_info.zw));
        const float normal_delta = 1.0 - saturate(dot(curr_n, prev_n));
        const float depth_delta = saturate(abs(tile_info.x - prev_tile_info.x) * 40.0);
        score += normal_delta * max(cb_srvs.assp_ray_budget_normal_delta_weight, 0.0);
        score += depth_delta * max(cb_srvs.assp_ray_budget_depth_delta_weight, 0.0);
    }

    const float budget_span = max(float(max_rays - min_rays), 0.0);
    const uint ray_count = (uint)clamp(round(float(min_rays) + score * budget_span), float(min_rays), float(max_rays));
    return ray_count;
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP, 1, 1)]
void main_cs(uint3 gtid : SV_GroupThreadID, uint gindex : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + gindex;
    const uint probe_count = AsspProbeTileCount();
    const uint ray_count = AsspComputeRayCount(probe_list_index, probe_count);

    gs_probe_ray_count[gindex] = ray_count;
    gs_probe_prefix_inclusive[gindex] = ray_count;
    GroupMemoryBarrierWithGroupSync();

#if 0
    // Reference implementation (naive):
    // single-lane serial prefix-sum over the group.
    if(0u == gindex)
    {
        uint running = 0u;
        [unroll]
        for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP; ++i)
        {
            running += gs_probe_ray_count[i];
            gs_probe_prefix_inclusive[i] = running;
        }
        gs_group_total_ray_count = running;
    }
    GroupMemoryBarrierWithGroupSync();
#else
    // Parallel prefix-sum (Blelloch scan) on shared memory.
    // 1) Up-sweep: build reduction tree.
    [unroll]
    for(uint stride = 1u; stride < ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP; stride <<= 1u)
    {
        const uint index = ((gindex + 1u) * (stride << 1u)) - 1u;
        if(index < ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP)
        {
            gs_probe_prefix_inclusive[index] += gs_probe_prefix_inclusive[index - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 2) Root holds total; convert to exclusive-scan seed.
    if(0u == gindex)
    {
        gs_group_total_ray_count = gs_probe_prefix_inclusive[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP - 1u];
        gs_probe_prefix_inclusive[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP - 1u] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // 3) Down-sweep: build exclusive prefix-sum.
    [unroll]
    for(uint stride = (ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP >> 1u); stride > 0u; stride >>= 1u)
    {
        const uint index = ((gindex + 1u) * (stride << 1u)) - 1u;
        if(index < ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP)
        {
            const uint left = index - stride;
            const uint t = gs_probe_prefix_inclusive[left];
            gs_probe_prefix_inclusive[left] = gs_probe_prefix_inclusive[index];
            gs_probe_prefix_inclusive[index] += t;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Convert exclusive -> inclusive for downstream offset computation.
    gs_probe_prefix_inclusive[gindex] += gs_probe_ray_count[gindex];
    GroupMemoryBarrierWithGroupSync();
#endif

    if(0u == gindex)
    {
        InterlockedAdd(RWAsspProbeTotalRayCountBuffer[0], gs_group_total_ray_count, gs_group_global_ray_offset_base);
    }
    GroupMemoryBarrierWithGroupSync();

    if(probe_list_index >= probe_count)
    {
        return;
    }

    // Probeごとのグローバルoffsetを確定.
    const uint ray_offset = gs_group_global_ray_offset_base + (gs_probe_prefix_inclusive[gindex] - ray_count);
    RWAsspProbeRayMetaBuffer[probe_list_index] = AsspPackRayMeta(ray_offset, ray_count);

    // RayQuery展開を同一パスで実行して FillRayQuery パスを不要化.
    [unroll]
    for(uint local_ray_index = 0u; local_ray_index < k_assp_ray_count_max; ++local_ray_index)
    {
        if(local_ray_index < ray_count)
        {
            RWAsspProbeRayQueryBuffer[ray_offset + local_ray_index] = AsspPackRayQuery(probe_list_index, local_ray_index);
        }
    }
}

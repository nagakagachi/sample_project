#if 0

assp_probe_variance_cs.hlsl

AdaptiveScreenSpaceProbe の 4x4 OctMap radiance から luminance moments を作り、
次フレームの coarse/fine 判定で使う temporal-stable variance signal を更新する。

#endif

#include "assp_probe_common.hlsli"

groupshared float gs_luminance_mean[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float gs_luminance_second_moment[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];

float AsspCalcLuminanceTemporalBlendRate(float curr_mean, float prev_mean)
{
    const float denom = max(max(curr_mean, prev_mean), 1e-4);
    const float relative_delta = abs(curr_mean - prev_mean) / denom;
    const float stability = 1.0 - saturate(relative_delta * 2.0);
    return clamp(stability, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint representative_probe_count = AsspRepresentativeProbeList[0];
    const uint probe_group_local_index = gindex / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + probe_group_local_index;
    const uint local_probe_texel_index = gindex % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint local_probe_lane = local_probe_texel_index;
    const bool is_probe_list_index_valid = probe_list_index < representative_probe_count;

    uint2 variance_tex_size;
    RWAdaptiveScreenSpaceProbeVarianceTex.GetDimensions(variance_tex_size.x, variance_tex_size.y);
    const int2 probe_tile_id = is_probe_list_index_valid ? AsspUnpackProbeTileId(AsspRepresentativeProbeList[probe_list_index + 1u]) : int2(-1, -1);
    const bool is_probe_tile_in_range = is_probe_list_index_valid && all(probe_tile_id >= 0) && all(probe_tile_id < int2(variance_tex_size));

    float luminance = 0.0;
    float second_moment = 0.0;
    if(is_probe_tile_in_range)
    {
        const int2 probe_atlas_local_pos = int2(local_probe_texel_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, local_probe_texel_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
        const int2 atlas_texel_pos = probe_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos;
        const float3 radiance = max(AdaptiveScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0)).rgb, 0.0.xxx);
        luminance = dot(radiance, float3(0.299, 0.587, 0.114));
        second_moment = luminance * luminance;
    }

    gs_luminance_mean[gindex] = luminance;
    gs_luminance_second_moment[gindex] = second_moment;
    GroupMemoryBarrierWithGroupSync();

    const uint probe_base_index = probe_group_local_index * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    [unroll]
    for(uint reduce_step = ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT >> 1u; reduce_step > 0u; reduce_step >>= 1u)
    {
        if(local_probe_lane < reduce_step)
        {
            const uint dst_index = probe_base_index + local_probe_lane;
            const uint src_index = dst_index + reduce_step;
            gs_luminance_mean[dst_index] += gs_luminance_mean[src_index];
            gs_luminance_second_moment[dst_index] += gs_luminance_second_moment[src_index];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if(local_probe_lane != 0u)
    {
        return;
    }

    if(!is_probe_tile_in_range)
    {
        return;
    }

    const float raw_mean = gs_luminance_mean[probe_base_index] * (1.0 / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT));
    const float raw_second_moment = gs_luminance_second_moment[probe_base_index] * (1.0 / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT));
    const float raw_variance = max(raw_second_moment - raw_mean * raw_mean, 0.0);

    float filtered_mean = raw_mean;
    float filtered_second_moment = raw_second_moment;

    if(0 != cb_srvs.ss_probe_temporal_reprojection_enable)
    {
        const uint best_prev_tile_packed = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_tile_id, 0)).x;
        if(0xffffffffu != best_prev_tile_packed)
        {
            const int2 best_prev_tile_id = AsspUnpackProbeTileId(best_prev_tile_packed);
            const float4 prev_variance_signal = AdaptiveScreenSpaceProbeHistoryVarianceTex.Load(int3(best_prev_tile_id, 0));
            const float temporal_rate = AsspCalcLuminanceTemporalBlendRate(raw_mean, prev_variance_signal.x);
            filtered_mean = lerp(raw_mean, prev_variance_signal.x, temporal_rate);
            filtered_second_moment = lerp(raw_second_moment, prev_variance_signal.y, temporal_rate);
        }
    }

    RWAdaptiveScreenSpaceProbeVarianceTex[probe_tile_id] = float4(
        filtered_mean,
        filtered_second_moment,
        raw_mean,
        raw_variance);
}

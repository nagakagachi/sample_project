#if 0

assp_probe_preupdate_cs.hlsl

AdaptiveScreenSpaceProbe ProbeTile 用の前処理。
全 4x4 tile を軽く走査し、active representative だけ tile info を更新して list 化する。

#endif

#include "assp_probe_common.hlsli"
#include "assp_buffer_util.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D TexHardwareDepth;

#define ASSP_TEMPORAL_SEARCH_RADIUS 1

void AsspStoreInvalidProbeTile(int2 probe_id)
{
    RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(1.0, uint2(0, 0), float2(0.0, 0.0), false);
    RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = 0xffffffffu;
}

void AsspPushRepresentativeProbe(uint2 probe_id)
{
    uint list_element_count = 0u;
    RWAsspRepresentativeProbeList.GetDimensions(list_element_count);

    uint old_count = 0u;
    InterlockedAdd(RWAsspRepresentativeProbeList[0], 1u, old_count);
    if((old_count + 1u) < list_element_count)
    {
        RWAsspRepresentativeProbeList[old_count + 1u] = AsspPackProbeTileId(probe_id);
    }
}

bool AsspTryLoadFrontDepthSample(
    int2 texel_pos,
    uint2 depth_size,
    out float probe_depth)
{
    probe_depth = 0.0;

    if(any(texel_pos < 0) || any(texel_pos >= int2(depth_size)))
        return false;

    const float depth = TexHardwareDepth.Load(int3(texel_pos, 0)).r;
    if(!isValidDepth(depth))
        return false;

    probe_depth = depth;
    return true;
}

float3 AsspCalcProbeNormalWs(int2 probe_texel_pos, float probe_depth, float2 depth_size_inv)
{
    const float3 probe_normal_vs = reconstruct_normal_vs_fine(
        TexHardwareDepth,
        probe_texel_pos,
        probe_depth,
        depth_size_inv,
        cb_ngl_sceneview.cb_ndc_z_to_view_z_coef,
        cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, probe_normal_vs);
    const float normal_len_sq = dot(probe_normal_ws, probe_normal_ws);
    return (normal_len_sq > 1e-8) ? (probe_normal_ws * rsqrt(normal_len_sq)) : float3(0.0, 0.0, 1.0);
}

bool AsspTryEvaluateHistoryRepresentativeCandidate(
    int2 history_lookup_tile_id,
    float2 depth_size_inv,
    float3 current_probe_pos_ws,
    float3 current_probe_normal_ws,
    out uint candidate_representative_tile_packed,
    out uint quantized_score)
{
    candidate_representative_tile_packed = 0xffffffffu;
    quantized_score = 0xffffffffu;

    uint2 history_tile_info_size;
    AdaptiveScreenSpaceProbeHistoryRepresentativeTileTex.GetDimensions(history_tile_info_size.x, history_tile_info_size.y);
    if(any(history_lookup_tile_id < 0) || any(history_lookup_tile_id >= int2(history_tile_info_size)))
    {
        return false;
    }

    candidate_representative_tile_packed = AdaptiveScreenSpaceProbeHistoryRepresentativeTileTex.Load(int3(history_lookup_tile_id, 0)).x;
    if(0xffffffffu == candidate_representative_tile_packed)
    {
        return false;
    }

    const int2 candidate_tile_id = AsspUnpackProbeTileId(candidate_representative_tile_packed);
    if(any(candidate_tile_id < 0) || any(candidate_tile_id >= int2(history_tile_info_size)))
    {
        return false;
    }

    const float4 candidate_tile_info = AdaptiveScreenSpaceProbeHistoryTileInfoTex.Load(int3(candidate_tile_id, 0));
    if(!isValidDepth(candidate_tile_info.x))
    {
        return false;
    }

    const int2 candidate_probe_texel = candidate_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE + AsspTileInfoDecodeProbePosInTile(candidate_tile_info.y);
    const float candidate_view_z = calc_view_z_from_ndc_z(candidate_tile_info.x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float3 candidate_pos_vs = CalcViewSpacePosition((float2(candidate_probe_texel) + 0.5) * depth_size_inv, candidate_view_z, cb_ngl_sceneview.cb_prev_proj_mtx);
    const float3 candidate_pos_ws = mul(cb_ngl_sceneview.cb_prev_view_inv_mtx, float4(candidate_pos_vs, 1.0));
    const float3 diff_ws = candidate_pos_ws - current_probe_pos_ws;
    const float plane_dist = abs(dot(diff_ws, current_probe_normal_ws));
    const float normal_dot = dot(current_probe_normal_ws, OctDecode(candidate_tile_info.zw));

    if(plane_dist >= cb_srvs.ss_probe_temporal_filter_plane_dist_threshold
        || normal_dot <= cb_srvs.ss_probe_temporal_filter_normal_cos_threshold)
    {
        return false;
    }

    float probe_dist = length(diff_ws) * 100.0;
    probe_dist += (1.0 - normal_dot);
    quantized_score = min((uint)(probe_dist * 1024.0), 0x03ffffffu);
    return true;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    uint2 tile_info_size;
    RWAdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    if(any(dtid.xy >= tile_info_size))
        return;

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);

    const int2 probe_id = int2(dtid.xy);
    const int2 tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const int2 representative_tile_id = AsspResolveRepresentativeTileId(tile_pixel_start);
    RWAdaptiveScreenSpaceProbeRepresentativeTileTex[probe_id] =
        any(representative_tile_id < 0) ? 0xffffffffu : AsspPackProbeTileId(uint2(representative_tile_id));
    if(any(representative_tile_id < 0) || any(representative_tile_id != probe_id))
    {
        AsspStoreInvalidProbeTile(probe_id);
        return;
    }

    uint2 probe_pos_in_tile = uint2(0, 0);
    int2 probe_texel_pos = tile_pixel_start;
    float probe_depth = 0.0;
    float front_linear_depth = 1e20;
    bool found_valid_probe = false;

    [unroll]
    for(int sy = 0; sy < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sy)
    {
        [unroll]
        for(int sx = 0; sx < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sx)
        {
            const uint2 local_probe_pos = uint2(sx, sy);
            const int2 sample_texel_pos = tile_pixel_start + int2(local_probe_pos);

            float sample_depth = 0.0;
            if(!AsspTryLoadFrontDepthSample(sample_texel_pos, depth_size, sample_depth))
            {
                continue;
            }

            const float sample_linear_depth = abs(calc_view_z_from_ndc_z(sample_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef));
            if(sample_linear_depth < front_linear_depth)
            {
                front_linear_depth = sample_linear_depth;
                probe_pos_in_tile = local_probe_pos;
                probe_texel_pos = sample_texel_pos;
                probe_depth = sample_depth;
                found_valid_probe = true;
            }
        }
    }

    if(!found_valid_probe)
    {
        AsspStoreInvalidProbeTile(probe_id);
        return;
    }

    const float3 probe_normal_ws = AsspCalcProbeNormalWs(probe_texel_pos, probe_depth, depth_size_inv);
    const float probe_view_z = calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * depth_size_inv;
    const float3 probe_pos_vs = CalcViewSpacePosition(probe_uv, probe_view_z, cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(probe_pos_vs, 1.0));

    uint best_prev_tile_packed = 0xffffffffu;
    if((0 != cb_srvs.ss_probe_temporal_reprojection_enable) && (cb_srvs.frame_count > 1))
    {
        bool is_valid_prev_uv = false;
        const float2 prev_uv = SspCalcPrevFrameUvFromWorldPos(probe_pos_ws, cb_ngl_sceneview.cb_prev_view_mtx, cb_ngl_sceneview.cb_prev_proj_mtx, is_valid_prev_uv);
        if(is_valid_prev_uv)
        {
            const int2 probe_tile_count = int2(tile_info_size);
            const float2 prev_pos_texel = prev_uv * float2(depth_size);
            const int2 prev_center_tile = clamp(int2(prev_pos_texel) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE, int2(0, 0), probe_tile_count - 1);

            uint direct_candidate_packed = 0xffffffffu;
            uint direct_candidate_score = 0xffffffffu;
            if(AsspTryEvaluateHistoryRepresentativeCandidate(prev_center_tile, depth_size_inv, probe_pos_ws, probe_normal_ws, direct_candidate_packed, direct_candidate_score))
            {
                best_prev_tile_packed = direct_candidate_packed;
            }
            else
            {
                uint best_candidate_score = 0xffffffffu;
                [unroll]
                for(int oy = -ASSP_TEMPORAL_SEARCH_RADIUS; oy <= ASSP_TEMPORAL_SEARCH_RADIUS; ++oy)
                {
                    [unroll]
                    for(int ox = -ASSP_TEMPORAL_SEARCH_RADIUS; ox <= ASSP_TEMPORAL_SEARCH_RADIUS; ++ox)
                    {
                        const int2 candidate_lookup_tile_id = clamp(prev_center_tile + int2(ox, oy), int2(0, 0), probe_tile_count - 1);

                        uint candidate_packed = 0xffffffffu;
                        uint candidate_score = 0xffffffffu;
                        if(!AsspTryEvaluateHistoryRepresentativeCandidate(candidate_lookup_tile_id, depth_size_inv, probe_pos_ws, probe_normal_ws, candidate_packed, candidate_score))
                        {
                            continue;
                        }

                        if(candidate_score < best_candidate_score)
                        {
                            best_candidate_score = candidate_score;
                            best_prev_tile_packed = candidate_packed;
                        }
                    }
                }
            }
        }
    }

    RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(probe_depth, probe_pos_in_tile, OctEncode(probe_normal_ws), 0xffffffffu != best_prev_tile_packed);
    RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = best_prev_tile_packed;
    AsspPushRepresentativeProbe(uint2(probe_id));
}

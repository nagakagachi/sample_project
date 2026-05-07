#include "assp_probe_common.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D TexHardwareDepth;

#define ASSP_TEMPORAL_SEARCH_RADIUS 1
#define ASSP_TILE_SAMPLE_COUNT (ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE)

// 代表点/代表法線の選択方針:
// 1) 4x4 内の有効サンプルからロバスト代表法線を作る（外れ値を inlier 再平均で抑制）
// 2) 深度・法線・中心距離の複合スコアで代表ピクセルを選ぶ（最前面1点固定をやめる）
// 3) 前フレーム位置が十分近い品質なら維持して、フレーム間ジャンプを抑える
// 微細段差でのちらつき/法線スパイク抑制に効くため、ASSP の安定化ロジックとして維持する。

void AsspStoreInvalidProbeTile(int2 probe_id)
{
    RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(1.0, uint2(0, 0), float2(0.0, 0.0), false);
    RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = 0xffffffffu;
}

bool AsspTryLoadFrontDepthSample(
    int2 texel_pos,
    uint2 depth_size,
    out float probe_depth)
{
    probe_depth = 0.0;
    if(any(texel_pos < 0) || any(texel_pos >= int2(depth_size)))
    {
        return false;
    }

    const float depth = TexHardwareDepth.Load(int3(texel_pos, 0)).r;
    if(!isValidDepth(depth))
    {
        return false;
    }

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

bool AsspTryEvaluateHistoryTileCandidate(
    int2 candidate_tile_id,
    float2 depth_size_inv,
    float3 current_probe_pos_ws,
    float3 current_probe_normal_ws,
    out uint quantized_score)
{
    quantized_score = 0xffffffffu;

    uint2 history_tile_info_size;
    AdaptiveScreenSpaceProbeHistoryTileInfoTex.GetDimensions(history_tile_info_size.x, history_tile_info_size.y);
    if(any(candidate_tile_id < int2(0, 0)) || any(candidate_tile_id >= int2(history_tile_info_size)))
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
    {
        return;
    }

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);

    const int2 probe_id = int2(dtid.xy);
    const int2 tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;

    // 4x4 タイル内サンプルを収集し、候補の深度/法線を保持する。
    bool sample_valid[ASSP_TILE_SAMPLE_COUNT];
    float sample_depth[ASSP_TILE_SAMPLE_COUNT];
    float sample_linear_depth[ASSP_TILE_SAMPLE_COUNT];
    float3 sample_normal_ws[ASSP_TILE_SAMPLE_COUNT];
    float depth_sum = 0.0;
    float3 normal_sum_ws = float3(0.0, 0.0, 0.0);
    uint valid_sample_count = 0u;
    [unroll]
    for(int sy = 0; sy < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sy)
    {
        [unroll]
        for(int sx = 0; sx < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sx)
        {
            const uint sample_index = uint(sy * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE + sx);
            sample_valid[sample_index] = false;
            sample_depth[sample_index] = 0.0;
            sample_linear_depth[sample_index] = 0.0;
            sample_normal_ws[sample_index] = float3(0.0, 0.0, 1.0);

            const int2 sample_texel_pos = tile_pixel_start + int2(sx, sy);
            float depth = 0.0;
            if(!AsspTryLoadFrontDepthSample(sample_texel_pos, depth_size, depth))
            {
                continue;
            }

            const float linear_depth = abs(calc_view_z_from_ndc_z(depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef));
            const float3 normal_ws = AsspCalcProbeNormalWs(sample_texel_pos, depth, depth_size_inv);

            sample_valid[sample_index] = true;
            sample_depth[sample_index] = depth;
            sample_linear_depth[sample_index] = linear_depth;
            sample_normal_ws[sample_index] = normal_ws;
            depth_sum += linear_depth;
            normal_sum_ws += normal_ws;
            valid_sample_count += 1u;
        }
    }

    if(0u == valid_sample_count)
    {
        AsspStoreInvalidProbeTile(probe_id);
        return;
    }

    // 代表法線はロバスト平均で決める（一次平均→inlier再平均）。
    // 単一点の法線を直接採用しないことで、微小段差/ノイズ起因の外れ値を弱める。
    const float mean_linear_depth = depth_sum / float(valid_sample_count);
    const float normal_sum_len_sq = dot(normal_sum_ws, normal_sum_ws);
    float3 representative_normal_ws = (normal_sum_len_sq > 1e-8) ? (normal_sum_ws * rsqrt(normal_sum_len_sq)) : float3(0.0, 0.0, 1.0);
    {
        const float k_normal_inlier_cos = 0.6;
        float3 refined_normal_sum_ws = float3(0.0, 0.0, 0.0);
        uint refined_count = 0u;
        [unroll]
        for(int sample_index = 0; sample_index < ASSP_TILE_SAMPLE_COUNT; ++sample_index)
        {
            if(!sample_valid[sample_index])
            {
                continue;
            }
            if(dot(sample_normal_ws[sample_index], representative_normal_ws) < k_normal_inlier_cos)
            {
                continue;
            }
            refined_normal_sum_ws += sample_normal_ws[sample_index];
            refined_count += 1u;
        }
        if(refined_count > 0u)
        {
            const float refined_len_sq = dot(refined_normal_sum_ws, refined_normal_sum_ws);
            representative_normal_ws = (refined_len_sq > 1e-8) ? (refined_normal_sum_ws * rsqrt(refined_len_sq)) : representative_normal_ws;
        }
    }

    // 代表ピクセルは深度/法線/中心距離の複合スコア最小で選ぶ。
    // 深度だけでなく法線一貫性を入れることで、見た目の面連続性を優先する。
    const float k_depth_term_weight = 2.0;
    const float k_normal_term_weight = 1.0;
    const float k_center_term_weight = 0.05;
    uint representative_sample_index = 0xffffffffu;
    float representative_score = 1e20;
    const float2 tile_center = float2(ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1, ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE - 1) * 0.5;
    [unroll]
    for(int sample_index = 0; sample_index < ASSP_TILE_SAMPLE_COUNT; ++sample_index)
    {
        if(!sample_valid[sample_index])
        {
            continue;
        }
        const int2 local_pos = int2(sample_index % ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE, sample_index / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
        const float depth_term = abs(sample_linear_depth[sample_index] - mean_linear_depth) / max(mean_linear_depth, 1e-3);
        const float normal_term = 1.0 - saturate(dot(sample_normal_ws[sample_index], representative_normal_ws));
        const float center_term = length(float2(local_pos) - tile_center) * k_center_term_weight;
        const float score = depth_term * k_depth_term_weight + normal_term * k_normal_term_weight + center_term;
        if(score < representative_score)
        {
            representative_score = score;
            representative_sample_index = sample_index;
        }
    }

    // 履歴位置が十分近い品質なら維持して、フレーム間のジャンプを抑える。
    const float4 history_tile_info = AdaptiveScreenSpaceProbeHistoryTileInfoTex.Load(int3(probe_id, 0));
    if(isValidDepth(history_tile_info.x))
    {
        const int2 history_probe_pos_in_tile = AsspTileInfoDecodeProbePosInTile(history_tile_info.y);
        const uint history_sample_index = uint(history_probe_pos_in_tile.y * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE + history_probe_pos_in_tile.x);
        if(history_sample_index < ASSP_TILE_SAMPLE_COUNT && sample_valid[history_sample_index])
        {
            const int2 history_local_pos = int2(history_sample_index % ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE, history_sample_index / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
            const float depth_term = abs(sample_linear_depth[history_sample_index] - mean_linear_depth) / max(mean_linear_depth, 1e-3);
            const float normal_term = 1.0 - saturate(dot(sample_normal_ws[history_sample_index], representative_normal_ws));
            const float center_term = length(float2(history_local_pos) - tile_center) * k_center_term_weight;
            const float history_score = depth_term * k_depth_term_weight + normal_term * k_normal_term_weight + center_term;
            const float k_hysteresis_margin = 0.05;
            if(history_score <= representative_score + k_hysteresis_margin)
            {
                representative_sample_index = history_sample_index;
                representative_score = history_score;
            }
        }
    }

    if(representative_sample_index == 0xffffffffu)
    {
        AsspStoreInvalidProbeTile(probe_id);
        return;
    }

    const uint2 probe_pos_in_tile = uint2(
        representative_sample_index % ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE,
        representative_sample_index / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
    const int2 probe_texel_pos = tile_pixel_start + int2(probe_pos_in_tile);
    const float probe_depth = sample_depth[representative_sample_index];
    const float3 selected_normal_ws = sample_normal_ws[representative_sample_index];
    const float3 final_probe_normal_ws =
        (dot(selected_normal_ws, representative_normal_ws) >= 0.5)
        ? representative_normal_ws
        : selected_normal_ws;

    const float probe_view_z = calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * depth_size_inv;
    const float3 probe_pos_vs = CalcViewSpacePosition(probe_uv, probe_view_z, cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(probe_pos_vs, 1.0));

    uint best_prev_tile_packed = 0xffffffffu;
    if((0 != cb_srvs.assp_temporal_reprojection_enable) && (cb_srvs.frame_count > 1))
    {
        bool is_valid_prev_uv = false;
        const float2 prev_uv = SspCalcPrevFrameUvFromWorldPos(probe_pos_ws, cb_ngl_sceneview.cb_prev_view_mtx, cb_ngl_sceneview.cb_prev_proj_mtx, is_valid_prev_uv);
        if(is_valid_prev_uv)
        {
            const int2 probe_tile_count = int2(tile_info_size);
            const float2 prev_pos_texel = prev_uv * float2(depth_size);
            const int2 prev_center_tile = clamp(int2(prev_pos_texel) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE, int2(0, 0), probe_tile_count - 1);

            uint best_candidate_score = 0xffffffffu;
            [unroll]
            for(int oy = -ASSP_TEMPORAL_SEARCH_RADIUS; oy <= ASSP_TEMPORAL_SEARCH_RADIUS; ++oy)
            {
                [unroll]
                for(int ox = -ASSP_TEMPORAL_SEARCH_RADIUS; ox <= ASSP_TEMPORAL_SEARCH_RADIUS; ++ox)
                {
                    const int2 candidate_tile_id = clamp(prev_center_tile + int2(ox, oy), int2(0, 0), probe_tile_count - 1);
                    uint candidate_score = 0xffffffffu;
                    if(!AsspTryEvaluateHistoryTileCandidate(candidate_tile_id, depth_size_inv, probe_pos_ws, final_probe_normal_ws, candidate_score))
                    {
                        continue;
                    }

                    if(candidate_score < best_candidate_score)
                    {
                        best_candidate_score = candidate_score;
                        best_prev_tile_packed = AsspPackProbeTileId(uint2(candidate_tile_id));
                    }
                }
            }
        }
    }

    RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(probe_depth, probe_pos_in_tile, OctEncode(final_probe_normal_ws), 0xffffffffu != best_prev_tile_packed);
    RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = best_prev_tile_packed;
}

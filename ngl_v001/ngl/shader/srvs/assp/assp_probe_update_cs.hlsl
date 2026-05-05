#if 0

assp_probe_update_cs.hlsl

ASSP 更新の Resolve パス。
RayTrace パスが書いた ray 結果バッファを参照して、probe oct texel を更新する。

#endif

#include "assp_probe_common.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define ASSP_RAY_RESULT_STRIDE 5
#define ASSP_RAY_RESULT_OCT_CELL 0
#define ASSP_RAY_RESULT_SKY_VIS 1
#define ASSP_RAY_RESULT_RAD_R 2
#define ASSP_RAY_RESULT_RAD_G 3
#define ASSP_RAY_RESULT_RAD_B 4
#define ASSP_RADIANCE_FIXED_POINT_SCALE 256.0

groupshared uint gs_best_prev_tile_packed[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP];

float AsspBiasedShadowPreservingTemporalFilterWeight(float curr_value, float prev_value)
{
    const float l1 = curr_value;
    const float l2 = prev_value;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    alpha = CalcSquare(clamp(alpha, 0.0, 0.98));
    return alpha;
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 gtid : SV_GroupThreadID,
    uint gindex : SV_GroupIndex,
    uint3 gid : SV_GroupID)
{
    const uint probe_count = AsspProbeTileCount();
    const uint probe_group_local_index = gindex / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + probe_group_local_index;
    const bool is_probe_list_index_valid = probe_list_index < probe_count;

    uint2 probe_tex_size;
    RWAdaptiveScreenSpaceProbeTex.GetDimensions(probe_tex_size.x, probe_tex_size.y);

    const uint local_probe_texel_index = gindex % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const int2 probe_atlas_local_pos = int2(local_probe_texel_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, local_probe_texel_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
    int2 probe_id = int2(-1, -1);
    if(is_probe_list_index_valid)
    {
        AsspTryGetProbeTileIdFromLinearIndex(probe_list_index, probe_id);
    }
    const int2 global_pos = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos;

    uint2 tile_info_size;
    RWAdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    const bool is_probe_id_valid =
        is_probe_list_index_valid &&
        all(probe_id >= int2(0, 0)) &&
        all(probe_id < int2(tile_info_size));
    const bool is_global_pos_valid =
        is_probe_id_valid &&
        all(global_pos >= int2(0, 0)) &&
        all(global_pos < int2(probe_tex_size));
    const float4 probe_tile_info = is_probe_id_valid ? AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_id, 0)) : float4(1.0, 0.0, 0.0, 0.0);
    const bool has_valid_probe_tile = is_probe_id_valid && isValidDepth(probe_tile_info.x);

    if(0u == local_probe_texel_index)
    {
        gs_best_prev_tile_packed[probe_group_local_index] = 0xffffffffu;
    }
    GroupMemoryBarrierWithGroupSync();

    const int2 probe_tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
    const int2 probe_pos_in_tile = AsspTileInfoDecodeProbePosInTile(probe_tile_info.y);
    const int2 probe_texel_pos = probe_tile_pixel_start + probe_pos_in_tile;
    const float probe_depth = probe_tile_info.x;

    if((0u == local_probe_texel_index) && has_valid_probe_tile)
    {
        gs_best_prev_tile_packed[probe_group_local_index] = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_id, 0)).x;
    }
    GroupMemoryBarrierWithGroupSync();

    const bool has_temporal_history = has_valid_probe_tile && (0xffffffffu != gs_best_prev_tile_packed[probe_group_local_index]);
    float4 prev_reprojected_probe_value = float4(0.0, 0.0, 0.0, 0.0);
    if(has_temporal_history)
    {
        const int2 prev_global_pos = clamp(
            AsspUnpackProbeTileId(gs_best_prev_tile_packed[probe_group_local_index]) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos,
            int2(0, 0),
            int2(probe_tex_size) - 1);
        prev_reprojected_probe_value = AdaptiveScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0));
    }

    if(is_global_pos_valid)
    {
        if(!has_valid_probe_tile)
        {
            RWAdaptiveScreenSpaceProbeTex[global_pos] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        const uint packed_meta = AsspProbeRayMetaBuffer[probe_list_index];
        const uint ray_offset = AsspUnpackRayMetaOffset(packed_meta);
        const uint ray_count = AsspUnpackRayMetaCount(packed_meta);

        uint hit_count = 0u;
        uint sum_sky_visibility_u = 0u;
        uint3 sum_radiance_u = uint3(0u, 0u, 0u);
        [loop]
        for(uint ray_index = 0u; ray_index < ray_count; ++ray_index)
        {
            const uint ray_word_offset = (ray_offset + ray_index) * ASSP_RAY_RESULT_STRIDE;
            const uint ray_oct_cell = AsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_OCT_CELL];
            if((0xffffffffu == ray_oct_cell) || (ray_oct_cell != local_probe_texel_index))
            {
                continue;
            }

            hit_count += 1u;
            sum_sky_visibility_u += AsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_SKY_VIS];
            sum_radiance_u.x += AsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_R];
            sum_radiance_u.y += AsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_G];
            sum_radiance_u.z += AsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_B];
        }

        const float inv_hit_count = (hit_count > 0u) ? (1.0 / float(hit_count)) : 1.0;
        const float sky_visibility = (hit_count > 0u) ? (float(sum_sky_visibility_u) * inv_hit_count) : prev_reprojected_probe_value.a;
        const float3 radiance = (hit_count > 0u)
            ? (float3(sum_radiance_u) * (inv_hit_count / ASSP_RADIANCE_FIXED_POINT_SCALE))
            : prev_reprojected_probe_value.rgb;

        float3 new_radiance = radiance;
        float new_sky_visibility = sky_visibility;
        float reprojection_succeed = 0.0;
        if(has_temporal_history && (0 != cb_srvs.assp_temporal_reprojection_enable))
        {
            float temporal_rate = AsspBiasedShadowPreservingTemporalFilterWeight(sky_visibility, prev_reprojected_probe_value.a);
            temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);
            new_radiance = lerp(new_radiance, prev_reprojected_probe_value.rgb, temporal_rate);
            new_sky_visibility = lerp(new_sky_visibility, prev_reprojected_probe_value.a, temporal_rate);
            reprojection_succeed = 1.0;
        }

        RWAdaptiveScreenSpaceProbeTex[global_pos] = float4(new_radiance, new_sky_visibility);
        if(0u == local_probe_texel_index)
        {
            RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(probe_depth, uint2(probe_pos_in_tile), probe_tile_info.zw, reprojection_succeed > 0.5);
        }
    }
}

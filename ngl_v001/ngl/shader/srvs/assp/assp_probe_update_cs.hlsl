#if 0

assp_probe_update_cs.hlsl

AdaptiveScreenSpaceProbe の最小更新版。
有効 4x4 tile に対して各 OctMap texel ごとに半球 1 サンプルを行い、
raw RGBA = radiance.rgb / sky_visibility を直接書き込む。

#endif

#include "assp_probe_common.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/rand_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

float AsspBiasedShadowPreservingTemporalFilterWeight(float curr_value, float prev_value)
{
    const float l1 = curr_value;
    const float l2 = prev_value;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    //alpha = CalcSquare(clamp(alpha, 0.0, 0.95));
    alpha = CalcSquare(clamp(alpha, 0.0, 0.98));
    return alpha;
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID)
{
    const uint representative_probe_count = AsspRepresentativeProbeList[0];
    const uint probe_group_local_index = gtid.x / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + probe_group_local_index;
    if(probe_list_index >= representative_probe_count)
    {
        return;
    }

    uint2 probe_tex_size;
    RWAdaptiveScreenSpaceProbeTex.GetDimensions(probe_tex_size.x, probe_tex_size.y);

    const uint local_probe_texel_index = gtid.x % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const int2 probe_atlas_local_pos = int2(local_probe_texel_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, local_probe_texel_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
    const int2 probe_id = AsspUnpackProbeTileId(AsspRepresentativeProbeList[probe_list_index + 1u]);
    const int2 global_pos = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos;
    if(any(global_pos >= int2(probe_tex_size)))
        return;

    uint2 tile_info_size;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    if(any(probe_id >= int2(tile_info_size)))
        return;

    const float4 probe_tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_id, 0));
    if(!isValidDepth(probe_tile_info.x))
    {
        RWAdaptiveScreenSpaceProbeTex[global_pos] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const int2 probe_tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const uint2 depth_size = cb_ngl_sceneview.cb_render_resolution;
    const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;

    const int2 probe_pos_in_tile = AsspTileInfoDecodeProbePosInTile(probe_tile_info.y);
    const int2 probe_texel_pos = probe_tile_pixel_start + probe_pos_in_tile;
    const float probe_depth = probe_tile_info.x;
    const float probe_view_z = calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * depth_size_inv;
    const float3 probe_pos_vs = CalcViewSpacePosition(probe_uv, probe_view_z, cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(probe_pos_vs, 1.0));
    const float3 probe_normal_ws = normalize(OctDecode(probe_tile_info.zw));

    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(float(global_pos.x), float(global_pos.y), float(cb_srvs.frame_count))));

    const float2 oct_uv = (float2(probe_atlas_local_pos) + rng.rand2()) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
    const float3 sample_ray_dir = SspDecodeDirByNormal(oct_uv, probe_normal_ws);

    const float ray_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_normal_offset_scale;
    const float3 sample_ray_origin = probe_pos_ws + probe_normal_ws * ray_normal_offset + sample_ray_dir * ray_start_offset;

    const float trace_distance = 30.0;
    int hit_voxel_index = -1;
    float4 debug_ray_info;
#if NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_UPDATE
    const float4 curr_ray_t_ws =
        trace_bbv_hibrick(
#else
    const float4 curr_ray_t_ws =
        trace_bbv(
#endif
            hit_voxel_index, debug_ray_info,
            sample_ray_origin, sample_ray_dir, trace_distance,
            cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
            cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

    const bool is_sky_visible = (curr_ray_t_ws.x < 0.0);
    const float sky_visibility = is_sky_visible ? 1.0 : 0.0;
    const float3 hit_radiance = is_sky_visible ? 0.0.xxx : max(BitmaskBrickVoxelOptionData[hit_voxel_index].resolved_radiance, 0.0.xxx);

    float3 new_radiance = hit_radiance;
    float new_sky_visibility = sky_visibility;
    if(0 != cb_srvs.ss_probe_temporal_reprojection_enable)
    {
        const uint best_prev_tile_packed = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_id, 0)).x;
        if(0xffffffffu != best_prev_tile_packed)
        {
            const int2 prev_global_pos = clamp(
                AsspUnpackProbeTileId(best_prev_tile_packed) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos,
                int2(0, 0),
                int2(probe_tex_size) - 1);
            const float4 prev_reprojected_probe_value = AdaptiveScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0));
            float temporal_rate = AsspBiasedShadowPreservingTemporalFilterWeight(sky_visibility, prev_reprojected_probe_value.a);
            temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);

            new_radiance = lerp(new_radiance, prev_reprojected_probe_value.rgb, temporal_rate);
            new_sky_visibility = lerp(new_sky_visibility, prev_reprojected_probe_value.a, temporal_rate);
        }
    }

    RWAdaptiveScreenSpaceProbeTex[global_pos] = float4(new_radiance, new_sky_visibility);
}

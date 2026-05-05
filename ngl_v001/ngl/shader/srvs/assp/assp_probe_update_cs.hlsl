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

#if !defined( NGL_ASSP_RAY_GUIDING_ENABLE )
#define NGL_ASSP_RAY_GUIDING_ENABLE 1
#endif

#if !defined( NGL_ASSP_RAY_GUIDING_VISIBILITY_PDF_BIAS )
#define NGL_ASSP_RAY_GUIDING_VISIBILITY_PDF_BIAS 0.03
#endif

#define ASSP_RAY_SAMPLE_ACCUM_STRIDE 5
#define ASSP_RAY_SAMPLE_ACCUM_COUNT 0
#define ASSP_RAY_SAMPLE_ACCUM_SKY_VISIBILITY 1
#define ASSP_RAY_SAMPLE_ACCUM_RADIANCE_R 2
#define ASSP_RAY_SAMPLE_ACCUM_RADIANCE_G 3
#define ASSP_RAY_SAMPLE_ACCUM_RADIANCE_B 4
#define ASSP_RADIANCE_FIXED_POINT_SCALE 256.0

groupshared uint gs_ray_sample_accum[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE * ASSP_RAY_SAMPLE_ACCUM_STRIDE];
groupshared float4 gs_temporal_reprojected_probe_value[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float gs_prev_guiding_weight[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float gs_guiding_cdf[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared uint gs_best_prev_tile_packed[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP];
groupshared uint gs_use_guiding[ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP];

uint AsspDebugFrameRandomIndex()
{
    // フレーム依存乱数を止めたいときは seed の frame 成分だけ固定する。
    return (0 != cb_srvs.assp_debug_freeze_frame_random_enable) ? 0u : uint(cb_srvs.frame_count);
}

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
    uint gindex : SV_GroupIndex,
    uint3 gid : SV_GroupID)
{
    const uint representative_probe_count = AsspRepresentativeProbeList[0];
    const uint probe_group_local_index = gindex / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + probe_group_local_index;
    const bool is_probe_list_index_valid = probe_list_index < representative_probe_count;

    uint2 probe_tex_size;
    RWAdaptiveScreenSpaceProbeTex.GetDimensions(probe_tex_size.x, probe_tex_size.y);

    const uint local_probe_texel_index = gindex % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_base_index = probe_group_local_index * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const int2 probe_atlas_local_pos = int2(local_probe_texel_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, local_probe_texel_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
    const int2 probe_id = is_probe_list_index_valid ? AsspUnpackProbeTileId(AsspRepresentativeProbeList[probe_list_index + 1u]) : int2(-1, -1);
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

    gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_COUNT] = 0u;
    gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_SKY_VISIBILITY] = 0u;
    gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_R] = 0u;
    gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_G] = 0u;
    gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_B] = 0u;
    gs_temporal_reprojected_probe_value[gindex] = float4(0.0, 0.0, 0.0, 0.0);
    gs_prev_guiding_weight[gindex] = 0.0;
    gs_guiding_cdf[gindex] = 0.0;
    if(0u == local_probe_texel_index)
    {
        gs_best_prev_tile_packed[probe_group_local_index] = 0xffffffffu;
        gs_use_guiding[probe_group_local_index] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

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

    if((0u == local_probe_texel_index) && has_valid_probe_tile)
    {
        gs_best_prev_tile_packed[probe_group_local_index] = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_id, 0)).x;
    }
    GroupMemoryBarrierWithGroupSync();

    const bool has_temporal_history = has_valid_probe_tile && (0xffffffffu != gs_best_prev_tile_packed[probe_group_local_index]);
    if(has_temporal_history)
    {
        const int2 prev_global_pos = clamp(
            AsspUnpackProbeTileId(gs_best_prev_tile_packed[probe_group_local_index]) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos,
            int2(0, 0),
            int2(probe_tex_size) - 1);
        gs_temporal_reprojected_probe_value[gindex] = AdaptiveScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0));
    }

    const uint frame_random_index = AsspDebugFrameRandomIndex();
    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(float(global_pos.x), float(global_pos.y), float(frame_random_index))));

    const float2 cell_oct_uv = (float2(probe_atlas_local_pos) + 0.5) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
    const float3 cell_dir_ws = SspDecodeDirByNormal(cell_oct_uv, probe_normal_ws);
    const float cell_octmap_normal_dot_probe_normal = max(0.0, dot(probe_normal_ws, cell_dir_ws));
    const float temporal_reprojected_luminance_for_guiding =
        ((0 == cb_srvs.assp_ray_guiding_enable) || !has_temporal_history)
        ? 0.0
        : dot(gs_temporal_reprojected_probe_value[gindex].rgb, float3(0.299, 0.587, 0.114));
    gs_prev_guiding_weight[gindex] =
        (has_valid_probe_tile && has_temporal_history)
        ? (temporal_reprojected_luminance_for_guiding + NGL_ASSP_RAY_GUIDING_VISIBILITY_PDF_BIAS) * cell_octmap_normal_dot_probe_normal
        : 0.0;
    GroupMemoryBarrierWithGroupSync();

    if(0u == local_probe_texel_index)
    {
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT; ++i)
        {
            cdf_sum += gs_prev_guiding_weight[probe_base_index + i];
            gs_guiding_cdf[probe_base_index + i] = cdf_sum;
        }

        const bool can_use_guiding =
            has_valid_probe_tile &&
            has_temporal_history &&
            (0 != cb_srvs.assp_ray_guiding_enable) &&
            (cdf_sum > 1e-6);
        gs_use_guiding[probe_group_local_index] = can_use_guiding ? 1u : 0u;
        if(can_use_guiding)
        {
            const float cdf_sum_inv = 1.0 / cdf_sum;
            [unroll]
            for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT; ++i)
            {
                gs_guiding_cdf[probe_base_index + i] *= cdf_sum_inv;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const float ray_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_normal_offset_scale;

    if(has_valid_probe_tile)
    {
        float3 basis_t_ws;
        float3 basis_b_ws;
        BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);
        float3 sample_ray_dir;
#if NGL_ASSP_RAY_GUIDING_ENABLE
        if(0u != gs_use_guiding[probe_group_local_index])
        {
            const float guiding_rand = rng.rand();
            uint selected_oct_cell_index = local_probe_texel_index;
            [unroll]
            for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT; ++i)
            {
                if(guiding_rand <= gs_guiding_cdf[probe_base_index + i])
                {
                    selected_oct_cell_index = i;
                    break;
                }
            }

            const uint2 selected_cell = uint2(selected_oct_cell_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, selected_oct_cell_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
            const float2 selected_oct_uv = (float2(selected_cell) + rng.rand2()) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
            sample_ray_dir = SspDecodeDirByNormal(selected_oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);
        }
        else
#endif
        {
            const float3 unit_v3 = random_unit_vector3(float3(float(global_pos.x), float(global_pos.y), float(local_probe_texel_index ^ frame_random_index)));
            const float3 local_dir = normalize(unit_v3 + float3(0.0, 0.0, 1.0));
            sample_ray_dir = local_dir.x * basis_t_ws + local_dir.y * basis_b_ws + local_dir.z * probe_normal_ws;
        }

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
        const float2 oct_uv = SspEncodeDirByNormal(sample_ray_dir, probe_normal_ws);
        const float3 hit_radiance = is_sky_visible ? 0.0.xxx : max(BitmaskBrickVoxelOptionData[hit_voxel_index].resolved_radiance, 0.0.xxx);
        const uint3 fixed_point_hit_radiance = (uint3)(hit_radiance * ASSP_RADIANCE_FIXED_POINT_SCALE + 0.5.xxx);
        const int2 oct_cell_id = clamp(int2(oct_uv * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION), int2(0, 0), int2(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION - 1, ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION - 1));
        const uint accum_index = probe_base_index + oct_cell_id.y * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + oct_cell_id.x;
        InterlockedAdd(gs_ray_sample_accum[accum_index * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_COUNT], 1u);
        InterlockedAdd(gs_ray_sample_accum[accum_index * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_SKY_VISIBILITY], uint(sky_visibility));
        InterlockedAdd(gs_ray_sample_accum[accum_index * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_R], fixed_point_hit_radiance.x);
        InterlockedAdd(gs_ray_sample_accum[accum_index * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_G], fixed_point_hit_radiance.y);
        InterlockedAdd(gs_ray_sample_accum[accum_index * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_B], fixed_point_hit_radiance.z);
    }
    GroupMemoryBarrierWithGroupSync();

    if(is_global_pos_valid)
    {
        if(!has_valid_probe_tile)
        {
            RWAdaptiveScreenSpaceProbeTex[global_pos] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        const uint hit_count = gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_COUNT];
        const float sum_sky_visibility = gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_SKY_VISIBILITY];
        const float3 sum_radiance = float3(
            gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_R],
            gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_G],
            gs_ray_sample_accum[gindex * ASSP_RAY_SAMPLE_ACCUM_STRIDE + ASSP_RAY_SAMPLE_ACCUM_RADIANCE_B]);

        const float4 prev_reprojected_probe_value = gs_temporal_reprojected_probe_value[gindex];
        const float inv_hit_count = (hit_count > 0u) ? (1.0 / float(hit_count)) : 1.0;
        const float sky_visibility = (hit_count > 0u) ? (sum_sky_visibility * inv_hit_count) : prev_reprojected_probe_value.a;
        const float3 radiance = (hit_count > 0u) ? (sum_radiance * (inv_hit_count / ASSP_RADIANCE_FIXED_POINT_SCALE)) : prev_reprojected_probe_value.rgb;

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

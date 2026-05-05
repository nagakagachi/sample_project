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

#define ASSP_RAY_RESULT_STRIDE 5
#define ASSP_RAY_RESULT_OCT_CELL 0
#define ASSP_RAY_RESULT_SKY_VIS 1
#define ASSP_RAY_RESULT_RAD_R 2
#define ASSP_RAY_RESULT_RAD_G 3
#define ASSP_RAY_RESULT_RAD_B 4
#define ASSP_RADIANCE_FIXED_POINT_SCALE 256.0

uint AsspDebugFrameRandomIndex()
{
    return (0 != cb_srvs.assp_debug_freeze_frame_random_enable) ? 0u : uint(cb_srvs.frame_count);
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint3 gtid : SV_GroupThreadID, uint gindex : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    const uint ray_global_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE + gindex;
    const uint total_ray_count = AsspProbeTotalRayCountBuffer[0];
    if(ray_global_index >= total_ray_count)
    {
        return;
    }
    const uint packed_query = AsspProbeRayQueryBuffer[ray_global_index];
    const uint probe_list_index = AsspUnpackRayQueryProbeListIndex(packed_query);
    const uint local_ray_index = AsspUnpackRayQueryLocalRayIndex(packed_query);

    int2 probe_id;
    if(!AsspTryGetProbeTileIdFromLinearIndex(probe_list_index, probe_id))
    {
        return;
    }

    const uint packed_meta = AsspProbeRayMetaBuffer[probe_list_index];
    const uint ray_count = AsspUnpackRayMetaCount(packed_meta);
    const uint ray_offset = AsspUnpackRayMetaOffset(packed_meta);
    if(local_ray_index >= ray_count)
    {
        return;
    }

    uint2 tile_info_size_u;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u.x, tile_info_size_u.y);
    const int2 tile_info_size = int2(tile_info_size_u);
    const bool is_probe_id_valid =
        all(probe_id >= int2(0, 0)) &&
        all(probe_id < tile_info_size);
    const float4 probe_tile_info = is_probe_id_valid ? AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_id, 0)) : float4(1.0, 0.0, 0.0, 0.0);
    const bool has_valid_probe_tile = is_probe_id_valid && isValidDepth(probe_tile_info.x);

    const uint ray_word_offset = (ray_offset + local_ray_index) * ASSP_RAY_RESULT_STRIDE;
    if(!has_valid_probe_tile)
    {
        RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_OCT_CELL] = 0xffffffffu;
        RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_SKY_VIS] = 0u;
        RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_R] = 0u;
        RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_G] = 0u;
        RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_B] = 0u;
        return;
    }

    const int2 probe_tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const int2 probe_pos_in_tile = AsspTileInfoDecodeProbePosInTile(probe_tile_info.y);
    const int2 probe_texel_pos = probe_tile_pixel_start + probe_pos_in_tile;
    const float probe_depth = probe_tile_info.x;
    const float probe_view_z = calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * cb_ngl_sceneview.cb_render_resolution_inv;
    const float3 probe_pos_vs = CalcViewSpacePosition(probe_uv, probe_view_z, cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(probe_pos_vs, 1.0));
    const float3 probe_normal_ws = normalize(OctDecode(probe_tile_info.zw));

    RandomInstance rng;
    const uint frame_random_index = AsspDebugFrameRandomIndex();
    rng.rngState = asuint(noise_float_to_float(float3(float(probe_id.x), float(probe_id.y), float(local_ray_index ^ frame_random_index))));

    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

    float3 sample_ray_dir;
#if NGL_ASSP_RAY_GUIDING_ENABLE
    if(0 != cb_srvs.assp_ray_guiding_enable)
    {
        int2 history_probe_id = probe_id;
        const uint best_prev_tile_packed = AdaptiveScreenSpaceProbeBestPrevTileTex.Load(int3(probe_id, 0)).x;
        if(0xffffffffu != best_prev_tile_packed)
        {
            history_probe_id = AsspUnpackProbeTileId(best_prev_tile_packed);
        }

        uint2 probe_tex_size_u;
        AdaptiveScreenSpaceProbeHistoryTex.GetDimensions(probe_tex_size_u.x, probe_tex_size_u.y);
        const int2 probe_tex_size = int2(probe_tex_size_u);

        float cdf[ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT];
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT; ++i)
        {
            const int2 local_pos = int2(i % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, i / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
            const int2 prev_global_pos = clamp(
                history_probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + local_pos,
                int2(0, 0),
                probe_tex_size - 1);
            const float3 prev_radiance = AdaptiveScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0)).rgb;
            const float prev_luminance = dot(prev_radiance, float3(0.299, 0.587, 0.114));
            const float2 oct_uv = (float2(local_pos) + 0.5) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, probe_normal_ws);
            cdf_sum += (prev_luminance + NGL_ASSP_RAY_GUIDING_VISIBILITY_PDF_BIAS) * max(0.0, dot(probe_normal_ws, dir_ws));
            cdf[i] = cdf_sum;
        }

        if(cdf_sum > 1e-6)
        {
            const float r = rng.rand() * cdf_sum;
            uint selected_oct_cell_index = 0u;
            [unroll]
            for(uint i = 0u; i < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT; ++i)
            {
                if(r <= cdf[i])
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
        {
            const float3 unit_v3 = random_unit_vector3(float3(float(probe_id.x), float(probe_id.y), float(local_ray_index + frame_random_index)));
            const float3 local_dir = normalize(unit_v3 + float3(0.0, 0.0, 1.0));
            sample_ray_dir = local_dir.x * basis_t_ws + local_dir.y * basis_b_ws + local_dir.z * probe_normal_ws;
        }
    }
    else
#endif
    {
        const float3 unit_v3 = random_unit_vector3(float3(float(probe_id.x), float(probe_id.y), float(local_ray_index + frame_random_index)));
        const float3 local_dir = normalize(unit_v3 + float3(0.0, 0.0, 1.0));
        sample_ray_dir = local_dir.x * basis_t_ws + local_dir.y * basis_b_ws + local_dir.z * probe_normal_ws;
    }

    const float ray_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_normal_offset_scale;
    const float3 sample_ray_origin = probe_pos_ws + probe_normal_ws * ray_normal_offset + sample_ray_dir * ray_start_offset;

    const float trace_distance = 30.0;
    int hit_voxel_index = -1;
    float4 debug_ray_info;
#if NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_UPDATE
    const float4 curr_ray_t_ws = trace_bbv_hibrick(
#else
    const float4 curr_ray_t_ws = trace_bbv(
#endif
        hit_voxel_index, debug_ray_info,
        sample_ray_origin, sample_ray_dir, trace_distance,
        cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
        cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

    const bool is_sky_visible = (curr_ray_t_ws.x < 0.0);
    const float2 oct_uv = SspEncodeDirByNormal(sample_ray_dir, probe_normal_ws);
    const float3 hit_radiance = is_sky_visible ? 0.0.xxx : max(BitmaskBrickVoxelOptionData[hit_voxel_index].resolved_radiance, 0.0.xxx);
    const uint3 fixed_point_hit_radiance = (uint3)(hit_radiance * ASSP_RADIANCE_FIXED_POINT_SCALE + 0.5.xxx);
    const int2 oct_cell_id = clamp(int2(oct_uv * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION), int2(0, 0), int2(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION - 1, ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION - 1));
    const uint oct_cell_flat = uint(oct_cell_id.y * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + oct_cell_id.x);

    RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_OCT_CELL] = oct_cell_flat;
    RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_SKY_VIS] = is_sky_visible ? 1u : 0u;
    RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_R] = fixed_point_hit_radiance.x;
    RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_G] = fixed_point_hit_radiance.y;
    RWAsspProbeRayResultBuffer[ray_word_offset + ASSP_RAY_RESULT_RAD_B] = fixed_point_hit_radiance.z;
}

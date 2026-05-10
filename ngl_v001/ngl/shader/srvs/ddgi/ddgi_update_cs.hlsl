/*
    ddgi_update_cs.hlsl
    Dense orthogonal grid DDGI update for clipmap cascades.
*/

#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint global_cell_index = dtid.x;
    const uint total_cell_count = uint(max(cb_srvs.ddgi_total_cell_count, 0));
    if(global_cell_index >= total_cell_count)
    {
        return;
    }

    const uint update_split = uint(max(cb_srvs.ddgi_update_split, 1));
    if((global_cell_index % update_split) != (uint(cb_srvs.frame_count) % update_split))
    {
        return;
    }

    uint cascade_index = 0;
    uint local_cell_index = 0;
    if(!DdgiDecodeGlobalCellIndex(global_cell_index, cascade_index, local_cell_index))
    {
        return;
    }

    const float3 probe_pos_ws = DdgiCalcCellCenterWs(cascade_index, local_cell_index);
    const float distance_norm_scale = rcp(max(cb_srvs.ddgi_distance_normalize_m, 1e-3));
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(k_fsp_probe_octmap_width * k_fsp_probe_octmap_width);

    float4 packed_sh_coeff0 = 0.0.xxxx;
    float4 packed_sh_coeff1 = 0.0.xxxx;
    float4 packed_sh_coeff2 = 0.0.xxxx;
    float4 packed_sh_coeff3 = 0.0.xxxx;
    float4 dist_mean_coeff0 = 0.0.xxxx;
    float4 dist_mean_coeff1 = 0.0.xxxx;
    float4 dist_mean_coeff2 = 0.0.xxxx;
    float4 dist_mean_coeff3 = 0.0.xxxx;
    float4 dist_mean2_coeff0 = 0.0.xxxx;
    float4 dist_mean2_coeff1 = 0.0.xxxx;
    float4 dist_mean2_coeff2 = 0.0.xxxx;
    float4 dist_mean2_coeff3 = 0.0.xxxx;

    [unroll]
    for(uint oy = 0; oy < k_fsp_probe_octmap_width; ++oy)
    {
        [unroll]
        for(uint ox = 0; ox < k_fsp_probe_octmap_width; ++ox)
        {
            const float2 oct_uv = (float2(ox, oy) + 0.5.xx) / float(k_fsp_probe_octmap_width);
            const float3 sample_ray_dir = OctDecode(oct_uv);

            const float trace_distance = k_fsp_probe_distance_max;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
#if NGL_SRVS_TRACE_USE_HIBRICK_FSP_VISIBLE_SURFACE_ELEMENT_UPDATE
            float4 curr_ray_t_ws = trace_bbv_hibrick(
#else
            float4 curr_ray_t_ws = trace_bbv(
#endif
                hit_voxel_index, debug_ray_info,
                probe_pos_ws, sample_ray_dir, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

            const bool is_sky_visible = (0.0 > curr_ray_t_ws.x);
            const float sky_visibility = is_sky_visible ? 1.0 : 0.0;
            const float3 hit_radiance = is_sky_visible
                ? 0.0.xxx
                : max(BitmaskBrickVoxelOptionData[hit_voxel_index].resolved_radiance, 0.0.xxx);
            const float hit_distance = is_sky_visible ? trace_distance : curr_ray_t_ws.x;
            const float d_norm = saturate(hit_distance * distance_norm_scale);
            const float d_norm2 = d_norm * d_norm;

            const float4 sh_basis = EvaluateL1ShBasis(sample_ray_dir);
            const float4 packed_sample = float4(sky_visibility, hit_radiance);

            packed_sh_coeff0 += packed_sample * sh_basis.x;
            packed_sh_coeff1 += packed_sample * sh_basis.y;
            packed_sh_coeff2 += packed_sample * sh_basis.z;
            packed_sh_coeff3 += packed_sample * sh_basis.w;

            dist_mean_coeff0 += d_norm * sh_basis.x;
            dist_mean_coeff1 += d_norm * sh_basis.y;
            dist_mean_coeff2 += d_norm * sh_basis.z;
            dist_mean_coeff3 += d_norm * sh_basis.w;

            dist_mean2_coeff0 += d_norm2 * sh_basis.x;
            dist_mean2_coeff1 += d_norm2 * sh_basis.y;
            dist_mean2_coeff2 += d_norm2 * sh_basis.z;
            dist_mean2_coeff3 += d_norm2 * sh_basis.w;
        }
    }

    const uint sh_base_index = global_cell_index * 4;
    RWDdgiProbePackedShBuffer[sh_base_index + 0] = packed_sh_coeff0 * texel_solid_angle;
    RWDdgiProbePackedShBuffer[sh_base_index + 1] = packed_sh_coeff1 * texel_solid_angle;
    RWDdgiProbePackedShBuffer[sh_base_index + 2] = packed_sh_coeff2 * texel_solid_angle;
    RWDdgiProbePackedShBuffer[sh_base_index + 3] = packed_sh_coeff3 * texel_solid_angle;

    const uint dist_base_index = global_cell_index * 8;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 0] = dist_mean_coeff0 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 1] = dist_mean_coeff1 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 2] = dist_mean_coeff2 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 3] = dist_mean_coeff3 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 4] = dist_mean2_coeff0 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 5] = dist_mean2_coeff1 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 6] = dist_mean2_coeff2 * texel_solid_angle;
    RWDdgiProbeDistanceMomentBuffer[dist_base_index + 7] = dist_mean2_coeff3 * texel_solid_angle;
}

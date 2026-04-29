#if 0

fsp_update_cs.hlsl

V1 では coarse ray sample を止め、probe pool の stale release と
active probe list build に使う。

#endif

#include "../srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define FSP_STALE_FRAME_THRESHOLD (30u)
#define FSP_OCTA_UPDATE_TEMPORAL_RATE (0.1)

void FspPushFreeProbeIndex(uint probe_index)
{
    uint old_count = 0;
    InterlockedAdd(RWFspProbeFreeStack[0], 1, old_count);
    const uint write_index = old_count + 1;
    if(write_index <= cb_srvs.fsp_probe_pool_size)
    {
        RWFspProbeFreeStack[write_index] = probe_index;
    }
}

void FspClearProbeAtlas(uint probe_index)
{
    const uint2 probe_2d_map_pos = FspProbeAtlasMapPos(probe_index);
    [unroll]
    for(int oct_j = 0; oct_j < k_fsp_probe_octmap_width; ++oct_j)
    {
        [unroll]
        for(int oct_i = 0; oct_i < k_fsp_probe_octmap_width; ++oct_i)
        {
            RWFspProbeAtlasTex[probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)] = 0.0.xxxx;
        }
    }
}

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    const uint probe_index = dtid.x;
    if(cb_srvs.fsp_probe_pool_size <= probe_index)
    {
        return;
    }

    FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[probe_index];
    if(0 == (probe_pool_data.flags & k_fsp_probe_flag_allocated))
    {
        return;
    }

    const uint owner_cell_index = probe_pool_data.owner_cell_index;
    const bool has_valid_owner = (owner_cell_index != k_fsp_invalid_probe_index);
    const uint frame_age = cb_srvs.frame_count - probe_pool_data.last_seen_frame;
    const bool is_stale = has_valid_owner && (FSP_STALE_FRAME_THRESHOLD < frame_age);

    if(is_stale)
    {
        if(RWFspCellProbeIndexBuffer[owner_cell_index] == probe_index)
        {
            RWFspCellProbeIndexBuffer[owner_cell_index] = k_fsp_invalid_probe_index;
            RWFspProbeBuffer[owner_cell_index] = (FspProbeData)0;
        }

        uint release_list_index = 0;
        InterlockedAdd(RWFspReleaseProbeList[0], 1, release_list_index);
        if(release_list_index < cb_srvs.fsp_release_probe_buffer_size)
        {
            RWFspReleaseProbeList[release_list_index + 1] = probe_index;
        }

        probe_pool_data.owner_cell_index = k_fsp_invalid_probe_index;
        probe_pool_data.flags = 0;
        probe_pool_data.probe_offset_v3 = 0;
        probe_pool_data.avg_sky_visibility = 0.0;
        probe_pool_data.debug_last_released_frame = cb_srvs.frame_count;
        RWFspProbePoolBuffer[probe_index] = probe_pool_data;
        FspClearProbeAtlas(probe_index);

        FspPushFreeProbeIndex(probe_index);
        return;
    }

    uint active_list_index = 0;
    InterlockedAdd(RWFspActiveProbeList[0], 1, active_list_index);
    if(active_list_index < cb_srvs.fsp_active_probe_buffer_size)
    {
        RWFspActiveProbeList[active_list_index + 1] = probe_index;
    }

    if(has_valid_owner)
    {
        RWFspProbeBuffer[owner_cell_index].probe_offset_v3 = probe_pool_data.probe_offset_v3;
        RWFspProbeBuffer[owner_cell_index].avg_sky_visibility = probe_pool_data.avg_sky_visibility;
    }

    if(!has_valid_owner)
    {
        return;
    }

    uint cascade_index = 0;
    uint local_cell_index = 0;
    if(!FspDecodeGlobalCellIndex(owner_cell_index, cascade_index, local_cell_index))
    {
        return;
    }

    const uint target_cascade_index = cb_srvs.frame_count % uint(max(cb_srvs.fsp_cascade_count, 1));
    if(cascade_index != target_cascade_index)
    {
        return;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 probe_offset = decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * (cascade.grid.cell_size * 0.5);
    const float3 probe_pos_ws = FspCalcCellCenterWs(cascade_index, local_cell_index) + probe_offset;

    const uint oct_cell_index = (cb_srvs.frame_count + probe_index * 17u) % (k_fsp_probe_octmap_width * k_fsp_probe_octmap_width);
    const uint2 oct_cell_id = uint2(oct_cell_index % k_fsp_probe_octmap_width, oct_cell_index / k_fsp_probe_octmap_width);
    const float2 oct_uv = (float2(oct_cell_id) + float2(0.5, 0.5)) / float(k_fsp_probe_octmap_width);
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

    const uint2 atlas_texel_pos = FspProbeAtlasTexelCoord(probe_index, oct_cell_id);
    const float4 atlas_prev = RWFspProbeAtlasTex[atlas_texel_pos];
    const float4 atlas_curr = float4(hit_radiance, sky_visibility);
    RWFspProbeAtlasTex[atlas_texel_pos] = lerp(atlas_prev, atlas_curr, FSP_OCTA_UPDATE_TEMPORAL_RATE);

    probe_pool_data.avg_sky_visibility = lerp(probe_pool_data.avg_sky_visibility, sky_visibility, FSP_OCTA_UPDATE_TEMPORAL_RATE);
    RWFspProbePoolBuffer[probe_index] = probe_pool_data;

    RWFspProbeBuffer[owner_cell_index].avg_sky_visibility = probe_pool_data.avg_sky_visibility;
}


#if 0

fsp_begin_update_cs.hlsl

各種バッファクリアや, 移動によって発生した領域のInValidateをする.
Dispatchは全域としているが, 最適化としてはInvalidate領域サイズ分だけにしたい.

#endif


#include "../srvs_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define FSP_STALE_FRAME_THRESHOLD (30u)

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

void FspPushCurrActiveProbeIndex(uint probe_index)
{
    uint active_list_index = 0;
    InterlockedAdd(RWFspActiveProbeListCurr[0], 1, active_list_index);
    if(active_list_index < cb_srvs.fsp_active_probe_buffer_size)
    {
        RWFspActiveProbeListCurr[active_list_index + 1] = probe_index;
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
    if(0 == dtid.x)
    {
        // アトミックカウンタをクリア. 0番目はアトミックカウンタ用に予約している.
        RWSurfaceProbeCellList[0] = 0;
        RWFspActiveProbeListCurr[0] = 0;
        RWFspReleaseProbeList[0] = 0;
    }

    const uint prev_active_probe_count = FspActiveProbeListPrev[0];
    if(dtid.x >= prev_active_probe_count)
    {
        return;
    }

    const uint probe_index = FspActiveProbeListPrev[dtid.x + 1];
    if(probe_index >= cb_srvs.fsp_probe_pool_size)
    {
        return;
    }

    FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[probe_index];
    if(0 == (probe_pool_data.flags & k_fsp_probe_flag_allocated))
    {
        return;
    }

    const uint owner_cell_index = probe_pool_data.owner_cell_index;
    bool is_invalidate_area = (owner_cell_index == k_fsp_invalid_probe_index);
    if(!is_invalidate_area)
    {
        uint cascade_index = 0;
        uint local_cell_index = 0;
        if(!FspDecodeGlobalCellIndex(owner_cell_index, cascade_index, local_cell_index))
        {
            is_invalidate_area = true;
        }
        else
        {
            const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
            int3 voxel_coord = index_to_voxel_coord(local_cell_index, cascade.grid.grid_resolution);
            const int3 linear_voxel_coord = (voxel_coord - cascade.grid.grid_toroidal_offset_prev + cascade.grid.grid_resolution) % cascade.grid.grid_resolution;
            const int3 voxel_coord_toroidal_curr = linear_voxel_coord - cascade.grid.grid_move_cell_delta;
            is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= cascade.grid.grid_resolution);
        }
    }

    const uint frame_age = cb_srvs.frame_count - probe_pool_data.last_seen_frame;
    const bool is_stale = !is_invalidate_area && (FSP_STALE_FRAME_THRESHOLD < frame_age);
    if(!is_invalidate_area && !is_stale)
    {
        FspPushCurrActiveProbeIndex(probe_index);
        return;
    }

    if(owner_cell_index != k_fsp_invalid_probe_index && RWFspCellProbeIndexBuffer[owner_cell_index] == probe_index)
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
    probe_pool_data.last_update_frame = 0;
    probe_pool_data.debug_last_released_frame = cb_srvs.frame_count;
    RWFspProbePoolBuffer[probe_index] = probe_pool_data;
    FspClearProbeAtlas(probe_index);

    FspPushFreeProbeIndex(probe_index);
}

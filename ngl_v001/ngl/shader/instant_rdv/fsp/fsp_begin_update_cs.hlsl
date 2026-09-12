
#if 0

fsp_begin_update_cs.hlsl

各種バッファクリアや, 移動によって発生した領域のInValidateをする.
前回のActiveProbeリストを対象にDispatchし、範囲外・期限切れのProbeを解放する。
解放するProbeの所有セルのIrradianceVolume SHもクリアする。非Activeセルは対象外。

#endif


#include "../instant_rdv_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define FSP_STALE_FRAME_THRESHOLD (120u)

void FspPushFreeProbeIndex(uint probe_index)
{
    uint old_count = 0;
    InterlockedAdd(RWFspProbeFreeStack[0], 1, old_count);
    const uint write_index = old_count + 1;
    if(write_index <= cb_instant_rdv.fsp_probe_pool_size)
    {
        RWFspProbeFreeStack[write_index] = probe_index;
    }
}

void FspPushCurrActiveProbeIndex(uint probe_index)
{
    uint active_list_index = 0;
    InterlockedAdd(
        RWFspActiveProbeListCurr[FspActiveProbeCurrentCounterSlot()],
        1,
        active_list_index);
    if(active_list_index < cb_instant_rdv.fsp_active_probe_buffer_size)
    {
        RWFspActiveProbeListCurr[FspActiveProbeListAddress(active_list_index)] = probe_index;
    }
}

void FspClearIrradianceVolumeCell(uint global_cell_index)
{
    [unroll]
    for(uint coeff_index = 0; coeff_index < k_fsp_irradiance_volume_sh_float4_count; ++coeff_index)
    {
        RWFspIrradianceVolumeSHBuffer[FspIrradianceVolumeSHAddress(global_cell_index, coeff_index)] = 0.0.xxxx;
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
        // Currentの未使用counterだけを次回の同一物理Buffer世代用に先行クリアする。
        // 使用中counterは他threadが生存Probeをappendするため、同じwordをresetしてはならない。
        RWSurfaceProbeCellList[0] = 0;
        RWFspActiveProbeListCurr[
            FspActiveProbeCurrentCounterSlot() ^ 1u] = 0;
        RWFspProbeRayRequestBuffer[0] = 0;
        RWFspProbeRayResultBuffer[0] = 0;
    }

    const uint prev_active_probe_count =
        FspActiveProbeListPrev[FspActiveProbePreviousCounterSlot()];
    if(dtid.x >= prev_active_probe_count)
    {
        return;
    }

    const uint probe_index =
        FspActiveProbeListPrev[FspActiveProbeListAddress(dtid.x)];
    if(probe_index >= cb_instant_rdv.fsp_probe_pool_size)
    {
        return;
    }

    FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[probe_index];
    if(0 == cb_instant_rdv.fsp_probe_lifecycle_enable)
    {
        if(probe_pool_data.owner_cell_index != k_fsp_invalid_probe_index)
        {
            FspPushCurrActiveProbeIndex(probe_index);
        }
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
            int3 voxel_coord =
                FspLocalCellIndexToPhysicalCoord(local_cell_index, cascade.grid.grid_resolution);
            const int3 linear_voxel_coord = (voxel_coord - cascade.grid.grid_toroidal_offset_prev + cascade.grid.grid_resolution) % cascade.grid.grid_resolution;
            const int3 voxel_coord_toroidal_curr = linear_voxel_coord - cascade.grid.grid_move_cell_delta;
            is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= cascade.grid.grid_resolution);
        }
    }

    const uint frame_age = cb_instant_rdv.frame_count - probe_pool_data.last_seen_frame;
    const bool is_stale = !is_invalidate_area && (FSP_STALE_FRAME_THRESHOLD < frame_age);
    if(!is_invalidate_area && !is_stale)
    {
        FspPushCurrActiveProbeIndex(probe_index);// 今回のフレームも生存.
        return;
    }

    if(owner_cell_index != k_fsp_invalid_probe_index && RWFspCellProbeIndexBuffer[owner_cell_index] == probe_index)
    {
        RWFspCellProbeIndexBuffer[owner_cell_index] = k_fsp_invalid_probe_index;
        FspClearIrradianceVolumeCell(owner_cell_index);
    }

    probe_pool_data.owner_cell_index = k_fsp_invalid_probe_index;
    probe_pool_data.reserved0 = 0;
    probe_pool_data.probe_offset_v3 = 0;
    probe_pool_data.last_update_frame = 0;
    probe_pool_data.reserved1 = 0;
    probe_pool_data.reserved2 = 0;
    RWFspProbePoolBuffer[probe_index] = probe_pool_data;

    FspPushFreeProbeIndex(probe_index);// 返却.
}

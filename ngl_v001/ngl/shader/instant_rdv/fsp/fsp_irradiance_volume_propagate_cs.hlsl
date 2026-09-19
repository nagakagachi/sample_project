#if 0

fsp_irradiance_volume_propagate_cs.hlsl
ファイル説明:
 ActiveProbeで直接更新されなかったFSP IrradianceVolumeセルへ、
 同一cascade内の6近傍からSHをcheckerboard伝播する。

#endif

#include "../instant_rdv_util.hlsli"

bool FspIsCellCenterOccupied(uint cascade_index, int3 linear_coord)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 cell_center_ws =
        (float3(linear_coord) + 0.5.xxx) * cascade.grid.cell_size + cascade.grid.grid_min_pos;
    return read_bbv_voxel_from_world_pos(
        BitmaskBrickVoxel,
        cb_instant_rdv.bbv.grid_resolution,
        cb_instant_rdv.bbv.grid_toroidal_offset,
        cb_instant_rdv.bbv.grid_min_pos,
        cb_instant_rdv.bbv.cell_size_inv,
        cell_center_ws) != 0u;
}

bool FspTryLoadNeighborSignals(
    out float4 out_sky_visibility,
    out float4 out_irradiance_r,
    out float4 out_irradiance_g,
    out float4 out_irradiance_b,
    uint cascade_index,
    int3 neighbor_linear_coord)
{
    out_sky_visibility = 0.0.xxxx;
    out_irradiance_r = 0.0.xxxx;
    out_irradiance_g = 0.0.xxxx;
    out_irradiance_b = 0.0.xxxx;

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    if(any(neighbor_linear_coord < 0) || any(neighbor_linear_coord >= cascade.grid.grid_resolution))
    {
        return false;
    }

    const uint neighbor_irradiance_volume_cell_index =
        FspIrradianceVolumeCellIndexFromLinearCoord(cascade_index, neighbor_linear_coord);
    FspIrradianceVolumeLoadSignalsRw(
        neighbor_irradiance_volume_cell_index,
        out_sky_visibility,
        out_irradiance_r,
        out_irradiance_g,
        out_irradiance_b);
    return FspIrradianceVolumeHasValidSignals(
        out_sky_visibility, out_irradiance_r, out_irradiance_g, out_irradiance_b);
}

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint irradiance_volume_cell_index = dtid.x;
    if(irradiance_volume_cell_index >= (uint)cb_instant_rdv.fsp_total_cell_count)
    {
        return;
    }

    uint cascade_index = 0u;
    uint irradiance_volume_local_cell_index = 0u;
    if(!FspDecodeGlobalCellIndex(
        irradiance_volume_cell_index,
        cascade_index,
        irradiance_volume_local_cell_index))
    {
        return;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const int3 physical_coord = FspLocalCellIndexToPhysicalCoord(
        irradiance_volume_local_cell_index,
        cascade.grid.grid_resolution);
    const int3 linear_coord =
        (physical_coord - cascade.grid.grid_toroidal_offset) & (cascade.grid.grid_resolution - 1);
    if(FspIsActiveProbeOwnedCell(irradiance_volume_cell_index))
    {
        // ActiveProbeのRT結果が最優先。伝播は未Activeの空間セルを埋めるだけで、観測セルは上書きしない。
        return;
    }

    const uint cell_parity = uint((linear_coord.x + linear_coord.y + linear_coord.z) & 1);
    // parityをフレームごとに切り替え、6近傍読みと同時書きの衝突を避ける。
    if(cell_parity != (cb_instant_rdv.frame_count & 1u))
    {
        return;
    }

    if(FspIsCellCenterOccupied(cascade_index, linear_coord))
    {
        // 不透明セル内部は注入点ではないため、近傍伝播でSHを作らない。
        return;
    }

    const int3 neighbor_offsets[6] =
    {
        int3( 1,  0,  0),
        int3(-1,  0,  0),
        int3( 0,  1,  0),
        int3( 0, -1,  0),
        int3( 0,  0,  1),
        int3( 0,  0, -1),
    };

    float4 accum_sky_visibility = 0.0.xxxx;
    float4 accum_irradiance_r = 0.0.xxxx;
    float4 accum_irradiance_g = 0.0.xxxx;
    float4 accum_irradiance_b = 0.0.xxxx;
    uint valid_neighbor_count = 0u;

    [unroll]
    for(uint neighbor_index = 0u; neighbor_index < 6u; ++neighbor_index)
    {
        float4 sky_visibility = 0.0.xxxx;
        float4 irradiance_r = 0.0.xxxx;
        float4 irradiance_g = 0.0.xxxx;
        float4 irradiance_b = 0.0.xxxx;
        if(!FspTryLoadNeighborSignals(
            sky_visibility,
            irradiance_r,
            irradiance_g,
            irradiance_b,
            cascade_index,
            linear_coord + neighbor_offsets[neighbor_index]))
        {
            continue;
        }

        accum_sky_visibility += sky_visibility;
        accum_irradiance_r += irradiance_r;
        accum_irradiance_g += irradiance_g;
        accum_irradiance_b += irradiance_b;
        ++valid_neighbor_count;
    }

    if(valid_neighbor_count == 0u)
    {
        return;
    }

    const float inv_count = rcp(float(valid_neighbor_count));
    FspIrradianceVolumeStoreSignals(
        irradiance_volume_cell_index,
        accum_sky_visibility * inv_count,
        accum_irradiance_r * inv_count,
        accum_irradiance_g * inv_count,
        accum_irradiance_b * inv_count);
}

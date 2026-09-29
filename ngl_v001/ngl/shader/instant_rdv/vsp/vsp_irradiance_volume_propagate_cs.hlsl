#if 0

vsp_irradiance_volume_propagate_cs.hlsl
ファイル説明:
 ActiveProbeで直接更新されなかったVSP IrradianceVolumeセルへ、
 同一cascade内の6近傍からSHをcheckerboard伝播する。

#endif

#include "../instant_rdv_util.hlsli"

bool VspIsCellCenterOccupied(uint cascade_index, int3 linear_coord)
{
    const VspCascadeGridParam cascade = VspGetCascadeParam(cascade_index);
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

bool VspTryLoadNeighborSignals(
    out float4 out_sky_visibility,
    out float4 out_irradiance_r,
    out float4 out_irradiance_g,
    out float4 out_irradiance_b,
    out bool out_is_active_probe,
    uint cascade_index,
    int3 neighbor_linear_coord)
{
    out_sky_visibility = 0.0.xxxx;
    out_irradiance_r = 0.0.xxxx;
    out_irradiance_g = 0.0.xxxx;
    out_irradiance_b = 0.0.xxxx;
    out_is_active_probe = false;

    const VspCascadeGridParam cascade = VspGetCascadeParam(cascade_index);
    if(any(neighbor_linear_coord < 0) || any(neighbor_linear_coord >= cascade.grid.grid_resolution))
    {
        return false;
    }

    const uint neighbor_irradiance_volume_cell_index =
        VspIrradianceVolumeCellIndexFromLinearCoord(cascade_index, neighbor_linear_coord);
    VspIrradianceVolumeLoadSignalsRw(
        neighbor_irradiance_volume_cell_index,
        out_sky_visibility,
        out_irradiance_r,
        out_irradiance_g,
        out_irradiance_b);
    const bool has_valid_signals = VspIrradianceVolumeHasValidSignals(
        out_sky_visibility, out_irradiance_r, out_irradiance_g, out_irradiance_b);
    if(!has_valid_signals)
    {
        return false;
    }

    if(0 != cb_instant_rdv.vsp_irradiance_volume_propagate_active_probe_weight_enable)
    {
        // OFF時は比較基準の伝播負荷を維持し、追加のActiveProbe参照を発行しない。
        out_is_active_probe = VspIsActiveProbeOwnedCell(neighbor_irradiance_volume_cell_index);
    }
    return true;
}

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint irradiance_volume_cell_index = dtid.x;
    if(irradiance_volume_cell_index >= (uint)cb_instant_rdv.vsp_total_cell_count)
    {
        return;
    }

    uint cascade_index = 0u;
    uint irradiance_volume_local_cell_index = 0u;
    if(!VspDecodeGlobalCellIndex(
        irradiance_volume_cell_index,
        cascade_index,
        irradiance_volume_local_cell_index))
    {
        return;
    }

    const VspCascadeGridParam cascade = VspGetCascadeParam(cascade_index);
    const int3 physical_coord = VspLocalCellIndexToPhysicalCoord(
        irradiance_volume_local_cell_index,
        cascade.grid.grid_resolution);
    const int3 linear_coord =
        (physical_coord - cascade.grid.grid_toroidal_offset) & (cascade.grid.grid_resolution - 1);
    if(VspIsActiveProbeOwnedCell(irradiance_volume_cell_index))
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

    if(VspIsCellCenterOccupied(cascade_index, linear_coord))
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
    float valid_neighbor_weight_sum = 0.0f;
    const float active_probe_neighbor_weight =
        (0 != cb_instant_rdv.vsp_irradiance_volume_propagate_active_probe_weight_enable)
        ? cb_instant_rdv.vsp_irradiance_volume_propagate_active_probe_weight_scale
        : 1.0f;

    [unroll]
    for(uint neighbor_index = 0u; neighbor_index < 6u; ++neighbor_index)
    {
        float4 sky_visibility = 0.0.xxxx;
        float4 irradiance_r = 0.0.xxxx;
        float4 irradiance_g = 0.0.xxxx;
        float4 irradiance_b = 0.0.xxxx;
        bool is_active_probe = false;
        if(!VspTryLoadNeighborSignals(
            sky_visibility,
            irradiance_r,
            irradiance_g,
            irradiance_b,
            is_active_probe,
            cascade_index,
            linear_coord + neighbor_offsets[neighbor_index]))
        {
            continue;
        }

        // ActiveProbeの直接更新値は、伝播済み近傍より大きい重みで優先できる。
        const float neighbor_weight = is_active_probe ? active_probe_neighbor_weight : 1.0f;
        accum_sky_visibility += sky_visibility * neighbor_weight;
        accum_irradiance_r += irradiance_r * neighbor_weight;
        accum_irradiance_g += irradiance_g * neighbor_weight;
        accum_irradiance_b += irradiance_b * neighbor_weight;
        valid_neighbor_weight_sum += neighbor_weight;
    }

    if(valid_neighbor_weight_sum == 0.0f)
    {
        return;
    }

    const float inv_count = rcp(valid_neighbor_weight_sum);
    VspIrradianceVolumeStoreSignals(
        irradiance_volume_cell_index,
        accum_sky_visibility * inv_count,
        accum_irradiance_r * inv_count,
        accum_irradiance_g * inv_count,
        accum_irradiance_b * inv_count);
}

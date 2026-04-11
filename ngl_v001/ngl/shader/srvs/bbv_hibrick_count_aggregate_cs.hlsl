#if 0

bbv_hibrick_count_aggregate_cs.hlsl

Brick occupied voxel count から HiBrick occupied voxel total count を再構築する.
HiBrick data region は logical 2x2x2 Brick cluster として保持し、
集計時に logical Brick 座標を current toroidal offset で physical Brick へ写して count を足し上げる。

#endif

#include "srvs_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid  : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex
)
{
    const uint hibrick_count = bbv_hibrick_count();
    if(dtid.x >= hibrick_count)
    {
        return;
    }

    const int3 hibrick_grid_resolution = bbv_hibrick_grid_resolution();
    const int3 hibrick_coord = index_to_voxel_coord(dtid.x, hibrick_grid_resolution);
    const int3 logical_brick_coord_min = hibrick_coord * k_bbv_hibrick_brick_resolution;
    const int3 logical_brick_coord_max = min(logical_brick_coord_min + int3(k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution), cb_srvs.bbv.grid_resolution);

    uint occupied_voxel_total_count = 0;
    [loop]
    for(int z = logical_brick_coord_min.z; z < logical_brick_coord_max.z; ++z)
    {
        [loop]
        for(int y = logical_brick_coord_min.y; y < logical_brick_coord_max.y; ++y)
        {
            [loop]
            for(int x = logical_brick_coord_min.x; x < logical_brick_coord_max.x; ++x)
            {
                // Brick count は physical BBV buffer 上にあるため、
                // logical Brick 座標を current toroidal offset で physical 座標へ変換して読む。
                const int3 physical_brick_coord = voxel_coord_toroidal_mapping(int3(x, y, z), cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_resolution);
                const uint physical_brick_index = voxel_coord_to_index(physical_brick_coord, cb_srvs.bbv.grid_resolution);
                occupied_voxel_total_count += RWBitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(physical_brick_index)];
            }
        }
    }

    // HiBrick data region は logical cluster 順の index をそのまま使う。
    RWBitmaskBrickVoxel[bbv_hibrick_voxel_count_addr(dtid.x)] = occupied_voxel_total_count;
}

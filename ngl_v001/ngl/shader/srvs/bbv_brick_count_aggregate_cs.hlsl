#if 0

bbv_brick_count_aggregate_cs.hlsl

BBV bitmask region から Brick occupied voxel count を再構築する.

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
    const uint brick_count = bbv_brick_count();
    if(dtid.x >= brick_count)
    {
        return;
    }

    const uint bitmask_addr = bbv_voxel_bitmask_data_addr(dtid.x);

    uint occupied_voxel_count = 0;
    for(uint i = 0; i < bbv_voxel_bitmask_uint_count(); ++i)
    {
        occupied_voxel_count += countbits(RWBitmaskBrickVoxel[bitmask_addr + i]);
    }

    RWBitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(dtid.x)] = occupied_voxel_count;
}

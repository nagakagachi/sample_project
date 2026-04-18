#if 0

bbv_brick_count_aggregate_cs.hlsl

BBV bitmask region から Brick occupied voxel count を再構築する.

#endif

#include "../srvs_util.hlsli"

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
#if NGL_SRVS_ENABLE_BRICK_LOCAL_AABB
    int3 local_coord_min = int3(k_bbv_per_voxel_resolution - 1, k_bbv_per_voxel_resolution - 1, k_bbv_per_voxel_resolution - 1);
    int3 local_coord_max = int3(0, 0, 0);
#endif
    for(uint i = 0; i < bbv_voxel_bitmask_uint_count(); ++i)
    {
        uint packed_bits = RWBitmaskBrickVoxel[bitmask_addr + i];
        occupied_voxel_count += countbits(packed_bits);
#if NGL_SRVS_ENABLE_BRICK_LOCAL_AABB
        while(0 != packed_bits)
        {
            const uint bit_index = firstbitlow(packed_bits);
            const int3 local_coord = int3(calc_bbv_bitcell_pos_from_bit_index(i * 32 + bit_index));
            local_coord_min = min(local_coord_min, local_coord);
            local_coord_max = max(local_coord_max, local_coord);
            packed_bits &= (packed_bits - 1);
        }
#endif
    }

    RWBitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(dtid.x)] = occupied_voxel_count;
#if NGL_SRVS_ENABLE_BRICK_LOCAL_AABB
    if(0 != occupied_voxel_count)
    {
        RWBitmaskBrickVoxel[bbv_voxel_brick_local_aabb_min_addr(dtid.x)] = bbv_pack_brick_local_aabb_coord(local_coord_min);
        RWBitmaskBrickVoxel[bbv_voxel_brick_local_aabb_max_addr(dtid.x)] = bbv_pack_brick_local_aabb_coord(local_coord_max);
    }
    else
    {
        RWBitmaskBrickVoxel[bbv_voxel_brick_local_aabb_min_addr(dtid.x)] = 0;
        RWBitmaskBrickVoxel[bbv_voxel_brick_local_aabb_max_addr(dtid.x)] = 0;
    }
#endif
}


#if 0

clear_voxel_cs.hlsl

#endif


#include "ssvg_util.hlsli"


// DepthBufferに対してDispatch.
[numthreads(128, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    uint voxel_count = cb_dispatch_param.base_grid_resolution.x * cb_dispatch_param.base_grid_resolution.y * cb_dispatch_param.base_grid_resolution.z;
    if(dtid.x < voxel_count)
    {
        RWCoarseVoxelBuffer[dtid.x] = empty_coarse_voxel_data();

        clear_voxel_data(RWOccupancyBitmaskVoxel, dtid.x);
    }
}

#if 0

clear_voxel_cs.hlsl

#endif


#include "ssvg_util.hlsli"


RWBuffer<uint>		RWBufferWork;
RWBuffer<uint>		RWOccupancyBitmaskVoxel;

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
    uint voxel_count = cb_dispatch_param.BaseResolution.x * cb_dispatch_param.BaseResolution.y * cb_dispatch_param.BaseResolution.z;
    if(dtid.x < voxel_count)
    {
        RWBufferWork[dtid.x] = 0;

        for(int i = 0; i < k_per_voxel_occupancy_u32_count; ++i)
        {
            RWOccupancyBitmaskVoxel[dtid.x * k_per_voxel_occupancy_u32_count + i] = 0;
        }
    }
}
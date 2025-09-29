
#if 0

generate_visible_voxel_indirect_arg_cs.hlsl

#endif


#include "ssvg_util.hlsli"

RWBuffer<uint>		RWVisibleVoxelIndirectArg;

// DepthBufferに対してDispatch.
[numthreads(1, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{

    const uint visible_voxel_count = VisibleCoarseVoxelList[0];
    RWVisibleVoxelIndirectArg[0] = (visible_voxel_count + (cb_ssvg.voxel_dispatch_thread_group_count.x - 1)) / cb_ssvg.voxel_dispatch_thread_group_count.x;
    RWVisibleVoxelIndirectArg[1] = 1;
    RWVisibleVoxelIndirectArg[2] = 1;
    
}

#if 0

bbv_generate_visible_surface_list_indirect_arg_cs.hlsl

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

    const uint visible_voxel_count = VisibleVoxelList[0];
    RWVisibleVoxelIndirectArg[0] = (visible_voxel_count + (cb_ssvg.bbv_indirect_cs_thread_group_size.x - 1)) / cb_ssvg.bbv_indirect_cs_thread_group_size.x;
    RWVisibleVoxelIndirectArg[1] = 1;
    RWVisibleVoxelIndirectArg[2] = 1;
    
}
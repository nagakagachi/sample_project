
#if 0

bbv_generate_remove_voxel_list_indirect_arg_cs.hlsl

#endif


#include "ssvg_util.hlsli"

RWBuffer<uint> RWRemoveVoxelIndirectArg;

// RemoveVoxelListを元にDispatchIndirectの引数を生成.
[numthreads(1, 1, 1)]
void main_cs(
    uint3 dtid  : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex
)
{
    const uint remove_count = RemoveVoxelList[0];
    RWRemoveVoxelIndirectArg[0] = (remove_count + (PROBE_UPDATE_THREAD_GROUP_SIZE - 1)) / PROBE_UPDATE_THREAD_GROUP_SIZE;
    RWRemoveVoxelIndirectArg[1] = 1;
    RWRemoveVoxelIndirectArg[2] = 1;
}

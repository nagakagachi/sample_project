
#if 0

bbv_remove_hollow_voxel_cs.hlsl

中空になったBitmaskBrickVoxelを除去する.

#endif

#include "ssvg_util.hlsli"

// とりあえず中空Voxelバッファに対してDispatch. Indirect化はあとで.
[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{

    // 除去Voxelリストの要素数チェック.
    if(dtid.x >= RemoveVoxelList[0])
    {
        return;
    }

    const uint element_index = (dtid.x + 1) * k_component_count_RemoveVoxelList;

    const uint voxel_index = RemoveVoxelList[(element_index)];
    const uint bitmask_u32_offset = RemoveVoxelList[(element_index) + 1];
    const uint clear_bitmask = RemoveVoxelList[(element_index) + 2];
    const uint _unused = RemoveVoxelList[(element_index) + 3];

    
    const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
    // 削除ビットを反転Andして除去.
    InterlockedAnd(RWBitmaskBrickVoxel[bbv_addr + bitmask_u32_offset], ~clear_bitmask);

    // 本当は unique_data_addr の非Emptyフラグも更新したいが一旦無しで.

}
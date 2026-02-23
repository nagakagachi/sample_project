
#if 0

bbv_removal_apply_cs.hlsl

中空になったBbvを除去する.
前段で作成した中空Voxelリストを参照して処理.

#endif

#include "ssvg_util.hlsli"

// 中空Voxelリストに対してDispatch.
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

    
    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
    // bitmask部に削除ビット反転Andを実行して除去.
    uint old_value;
    InterlockedAnd(RWBitmaskBrickVoxel[bbv_addr + bitmask_u32_offset], ~clear_bitmask, old_value);
    // 前回値に操作した結果が0となった場合は対応するComponentのOccupiedフラグも落とす.
    if(0 == (old_value & (~clear_bitmask)))
    {
        // Occupiedフラグの該当ビットを落とす.
        InterlockedAnd(RWBitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(voxel_index)], ~(1 << bitmask_u32_offset));
    }
}

#if 0

coarse_voxel_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

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
    const uint voxel_count = cb_dispatch_param.BaseResolution.x * cb_dispatch_param.BaseResolution.y * cb_dispatch_param.BaseResolution.z;
    
    const uint voxel_index = dtid.x;
    if(voxel_index < voxel_count)
    {
        const uint unique_data_addr = voxel_unique_data_addr(voxel_index);
        const uint obm_addr = voxel_occupancy_bitmask_data_addr(voxel_index);

        const int3 voxel_coord = index_to_voxel_coord(voxel_index, cb_dispatch_param.BaseResolution);
        const float3 voxel_pos_ws = (float3(voxel_coord) + 0.5) * cb_dispatch_param.CellSize + cb_dispatch_param.GridMinPos;

        uint obm_count = 0;
        for(uint i = 0; i < voxel_occupancy_bitmask_uint_count(); i++)
        {
            obm_count += CountBits32(RWOccupancyBitmaskVoxel[obm_addr + i]);
        }

        RWOccupancyBitmaskVoxel[unique_data_addr] = obm_count;// ユニークデータ部に占有ビット総数書き込み.
    }
}
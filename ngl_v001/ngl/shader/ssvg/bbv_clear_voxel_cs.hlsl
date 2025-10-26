
#if 0

bbv_clear_voxel_cs.hlsl

#endif


#include "ssvg_util.hlsli"


// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    uint voxel_count = cb_ssvg.bbv.grid_resolution.x * cb_ssvg.bbv.grid_resolution.y * cb_ssvg.bbv.grid_resolution.z;
    if(dtid.x < voxel_count)
    {
        RWBitmaskBrickVoxelOptionData[dtid.x] = (BbvOptionalData)0;

        clear_voxel_data(RWBitmaskBrickVoxel, dtid.x);
    }
}
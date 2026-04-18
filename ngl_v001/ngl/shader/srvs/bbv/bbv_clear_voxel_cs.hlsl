
#if 0

bbv_clear_voxel_cs.hlsl

#endif


#include "../srvs_util.hlsli"


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
    uint voxel_count = cb_srvs.bbv.grid_resolution.x * cb_srvs.bbv.grid_resolution.y * cb_srvs.bbv.grid_resolution.z;
    if(dtid.x < voxel_count)
    {
        RWBitmaskBrickVoxelOptionData[dtid.x] = (BbvOptionalData)0;

        clear_voxel_data(RWBitmaskBrickVoxel, dtid.x);
    }
    if(dtid.x < bbv_hibrick_count())
    {
        RWBitmaskBrickVoxel[bbv_hibrick_voxel_count_addr(dtid.x)] = 0;
    }
}

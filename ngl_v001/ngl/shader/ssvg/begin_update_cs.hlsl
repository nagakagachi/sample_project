
#if 0

begin_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
ConstantBuffer<DispatchParam> cb_dispatch_param;

RWBuffer<uint>		RWBufferWork;

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
        int next_value = RWBufferWork[dtid.x] - 1;
        RWBufferWork[dtid.x] = clamp(next_value, 0, 1000);
    }
}
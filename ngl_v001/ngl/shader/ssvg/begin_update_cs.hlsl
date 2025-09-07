
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
        int3 voxel_coord = addr_to_voxel_coord(dtid.x, cb_dispatch_param.BaseResolution);

        // 移動によるInvalidateチェック. 1ボクセル分だけ反対側に入り込んでしまうが, 条件調査が難しいため暫定とする.
        int3 linear_voxel_coord = (voxel_coord - cb_dispatch_param.GridToroidalOffsetPrev + cb_dispatch_param.BaseResolution) % cb_dispatch_param.BaseResolution;
        int3 voxel_coord_toroidal_curr = linear_voxel_coord + cb_dispatch_param.GridCellDelta;
        // 範囲外の領域に進行した場合はその領域をInvalidate.
        bool is_invalidate_area = any(voxel_coord_toroidal_curr <= 0) || any(voxel_coord_toroidal_curr >= (cb_dispatch_param.BaseResolution-1));


        int next_value = RWBufferWork[dtid.x] - 1;
        if(is_invalidate_area)
        {
            next_value = 0; // Invalidate領域は即座に0.
        }

        RWBufferWork[dtid.x] = clamp(next_value, 0, 5000);
    }
}
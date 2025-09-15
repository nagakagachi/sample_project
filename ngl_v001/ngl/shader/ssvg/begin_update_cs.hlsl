
#if 0

begin_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

RWBuffer<uint>		RWBufferWork;
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
    // 全Voxelをクリア.
    uint voxel_count = cb_dispatch_param.BaseResolution.x * cb_dispatch_param.BaseResolution.y * cb_dispatch_param.BaseResolution.z;

    /*
        coarse_voxel_update_cs.hlsl と同様に一端coord化してからtoroidalマッピングを考慮してインデックス化するように統一したほうがよいかも.

        // toroidalマッピング考慮
        const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.BaseResolution);
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);
    */


    if(dtid.x < voxel_count)
    {
        int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.BaseResolution);

        // 移動によるInvalidateチェック..
        // バッファ上のVoxelアドレスをToroidalマッピング前の座標に変換. 修正版.
        int3 linear_voxel_coord = (voxel_coord - cb_dispatch_param.GridToroidalOffsetPrev + cb_dispatch_param.BaseResolution) % cb_dispatch_param.BaseResolution;
        int3 voxel_coord_toroidal_curr = linear_voxel_coord - cb_dispatch_param.GridCellDelta;
        bool is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= (cb_dispatch_param.BaseResolution));// 範囲外の領域に進行した場合はその領域をInvalidate.

        if(is_invalidate_area)
        {
            // 移動によってシフトしてきた無効領域.
            RWBufferWork[dtid.x] = 0;

            // クリア.
            clear_voxel_data(RWOccupancyBitmaskVoxel, dtid.x);
        }
        else
        {
        }
    }
}
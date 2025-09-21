
#if 0

begin_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

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
    uint voxel_count = cb_dispatch_param.base_grid_resolution.x * cb_dispatch_param.base_grid_resolution.y * cb_dispatch_param.base_grid_resolution.z;

    #if 1
        if(dtid.x < voxel_count)
        {
            int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.base_grid_resolution);
            // 移動によるInvalidateチェック..
            // バッファ上のVoxelアドレスをToroidalマッピング前の座標に変換. 修正版.
            int3 linear_voxel_coord = (voxel_coord - cb_dispatch_param.grid_toroidal_offset_prev + cb_dispatch_param.base_grid_resolution) % cb_dispatch_param.base_grid_resolution;
            int3 voxel_coord_toroidal_curr = linear_voxel_coord - cb_dispatch_param.grid_move_cell_delta;
            bool is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= (cb_dispatch_param.base_grid_resolution));// 範囲外の領域に進行した場合はその領域をInvalidate.

            if(is_invalidate_area)
            {
                // 移動によってシフトしてきた無効領域.
                RWCoarseVoxelBuffer[dtid.x] = empty_coarse_voxel_data();
                clear_voxel_data(RWOccupancyBitmaskVoxel, dtid.x);
            }
        }
    #else
        // 将来的にはこちらの処理の方針にしたい. インデックス->VoxelCoord->ToroidalMapping->VoxelAddr.
        // toroidalマッピング考慮. できればこちらに移行したいがまだ境界部が怪しいので保留.
        const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.base_grid_resolution);
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.grid_toroidal_offset, cb_dispatch_param.base_grid_resolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_dispatch_param.base_grid_resolution);
        
        if(voxel_index < voxel_count)
        {
            bool is_invalidate_area = any(voxel_coord < cb_dispatch_param.grid_move_cell_delta) || any(voxel_coord >= (cb_dispatch_param.base_grid_resolution - int3(1,1,1) - cb_dispatch_param.grid_move_cell_delta));// 範囲外の領域に進行した場合はその領域をInvalidate.

            if(is_invalidate_area)
            {
                // 移動によってシフトしてきた無効領域.
                RWCoarseVoxelBuffer[voxel_index] = empty_coarse_voxel_data();
                clear_voxel_data(RWOccupancyBitmaskVoxel, voxel_index);
            }
        }
    #endif
}
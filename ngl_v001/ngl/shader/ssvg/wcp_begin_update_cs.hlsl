
#if 0

wcp_begin_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    uint probe_count = cb_ssvg.wcp.grid_resolution.x * cb_ssvg.wcp.grid_resolution.y * cb_ssvg.wcp.grid_resolution.z;

    if(all(cb_ssvg.wcp.grid_move_cell_delta == int3(0,0,0)))
    {
        // 移動無しなら何もしない.
        return;
    }

    if(dtid.x < probe_count)
    {
        int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_ssvg.wcp.grid_resolution);
        // 移動によるInvalidateチェック..
        // バッファ上のVoxelアドレスをToroidalマッピング前の座標に変換. 修正版.
        int3 linear_voxel_coord = (voxel_coord - cb_ssvg.wcp.grid_toroidal_offset_prev + cb_ssvg.wcp.grid_resolution) % cb_ssvg.wcp.grid_resolution;
        int3 voxel_coord_toroidal_curr = linear_voxel_coord - cb_ssvg.wcp.grid_move_cell_delta;
        bool is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= (cb_ssvg.wcp.grid_resolution));// 範囲外の領域に進行した場合はその領域をInvalidate.

        if(is_invalidate_area)
        {
            // 移動によってシフトしてきた無効領域.
            RWWcpProbeBuffer[dtid.x] = (WcpProbeData)0;
        }
    }
}
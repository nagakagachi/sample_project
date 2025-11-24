
#if 0

bbv_visible_surface_element_update_cs.hlsl

可視サーフェイス上のBbv要素に対する処理.
変更されたBbvに応じてOccupiedフラグの更新.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"


// Probeの更新で発行するレイトレース数.
#define RAY_SAMPLE_COUNT_PER_VOXEL 8
#define PROBE_UPDATE_TEMPORAL_RATE  (0.1)

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

    // VisibleCoarseVoxelListを利用するバージョン.
    const uint visible_voxel_count = VisibleVoxelList[0]; // 0番目にアトミックカウンタが入っている.
    const uint update_element_index = (dtid.x * (BBV_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(BBV_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1)));
    if(visible_voxel_count < update_element_index)
        return;

    const uint voxel_index = VisibleVoxelList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
    // voxel_indexからtoroidal考慮したVoxelIDを計算する.
    int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.bbv.grid_resolution);
    int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution -cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);

    const uint bbv_coarse_occupancy_info_addr = bbv_voxel_coarse_occupancy_info_addr(voxel_index);
    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);

    // TODO. Surface上のVoxel情報を更新するなど.
    //BbvOptionalData voxel_optional_data = BitmaskBrickVoxelOptionData[voxel_index];
}


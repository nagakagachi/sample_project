
#if 0

visible_probe_post_update_cs.hlsl

// 可視Probe更新の後処理.

#endif



#define PROBE_UPDATE_TEMPORAL_RATE (0.025)


#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

// DepthBufferに対してDispatch.
[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // VisibleCoarseVoxelListを利用するバージョン.
    const uint visible_voxel_count = VisibleCoarseVoxelList[0]; // 0番目にアトミックカウンタが入っている.
    const uint update_element_index = (dtid.x * (FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1)));
    
    if(visible_voxel_count < update_element_index)
        return;

    const uint voxel_index = VisibleCoarseVoxelList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
    // voxel_indexからtoroidal考慮したVoxelIDを計算する.
    int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.base_grid_resolution);
    int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.base_grid_resolution -cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);

    const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.probe_atlas_texture_base_width, voxel_index / cb_ssvg.probe_atlas_texture_base_width);
    // バッファの内容をAtlasへ反映.
    for(int i = 0; i < k_per_probe_texel_count; ++i)
    {
        const float sky_visibility = UpdateProbeWork[update_element_index * k_per_probe_texel_count + i];
        if(0.0 > sky_visibility)
            continue; // 負数の場合はサンプルがなかったとして更新無し.

        // 境界部込のテクセル位置.
        const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_probe_octmap_width_with_border + 1 + uint2(i % k_probe_octmap_width, i / k_probe_octmap_width);
        // 更新.
        RWTexProbeSkyVisibility[octmap_atlas_texel_pos] = lerp(RWTexProbeSkyVisibility[octmap_atlas_texel_pos], sky_visibility, PROBE_UPDATE_TEMPORAL_RATE);
    }
}
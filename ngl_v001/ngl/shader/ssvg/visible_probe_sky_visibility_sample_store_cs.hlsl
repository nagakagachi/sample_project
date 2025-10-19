
#if 0

visible_probe_sky_visibility_sample_store_cs.hlsl

// 可視Probe SkyVisibilityサンプルの結果をバッファに書き戻し.

#endif



#define PROBE_UPDATE_TEMPORAL_RATE (0.025)


#include "ssvg_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // VisibleCoarseVoxelListを利用するバージョン.
    const uint visible_voxel_count = VisibleVoxelList[0]; // 0番目にアトミックカウンタが入っている.
    const uint update_element_index = (dtid.x * (FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1)));
    
    if(visible_voxel_count < update_element_index)
        return;

    const uint voxel_index = VisibleVoxelList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
    // voxel_indexからtoroidal考慮したVoxelIDを計算する.
    int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.bbv.grid_resolution);
    int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution -cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);

    const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.bbv.flatten_2d_width, voxel_index / cb_ssvg.bbv.flatten_2d_width);
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

#if 0

fill_probe_sky_visibility_octmap_border_cs.hlsl

OctahedralMapの境界部を内側の値で埋める.

全Probeへの処理.

更新されたProbeのみにしたり, 更に境界部が更新されたProbeのみにするといった最適化をしたい.

#endif


#include "ssvg_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    const uint voxel_count = cb_ssvg.bbv.grid_resolution.x * cb_ssvg.bbv.grid_resolution.y * cb_ssvg.bbv.grid_resolution.z;
    // 動作検証のためこのシェーダはスキップなしの全体更新.
    const uint update_element_id = dtid.x;

    if(voxel_count <= update_element_id)
        return;

    const int3 voxel_coord = index_to_voxel_coord(update_element_id, cb_ssvg.bbv.grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution);

    
    const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.bbv.flatten_2d_width, voxel_index / cb_ssvg.bbv.flatten_2d_width);
    
    // 境界部込のOctmap最小位置.
    const uint2 octmap_atlas_texel_pos_min = probe_2d_map_pos * k_probe_octmap_width_with_border;
    
    // 頂点.
    RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(0, 0)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-2, k_probe_octmap_width_with_border-2)];
    RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-1, 0)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(1, k_probe_octmap_width_with_border-2)];
    RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(0, k_probe_octmap_width_with_border-1)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-2, 1)];
    RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-1, k_probe_octmap_width_with_border-1)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(1, 1)];
    // 左上頂点のエッジ.
    for(int i = 1; i < k_probe_octmap_width_with_border-1; ++i)
    {
        RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(i, 0)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-1 - i, 1)];
        RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(0, i)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(1, k_probe_octmap_width_with_border-1 - i)];
    }
    // 右下頂点のエッジ.
    for(int i = 1; i < k_probe_octmap_width_with_border-1; ++i)
    {
        RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-1, i)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-2, k_probe_octmap_width_with_border-1 - i)];
        RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(i, k_probe_octmap_width_with_border-1)] = RWTexProbeSkyVisibility[octmap_atlas_texel_pos_min + int2(k_probe_octmap_width_with_border-1 - i, k_probe_octmap_width_with_border-2)];
    }
}
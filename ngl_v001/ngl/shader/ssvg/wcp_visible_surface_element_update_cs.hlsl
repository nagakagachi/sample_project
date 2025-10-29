
#if 0

wcp_visible_surface_element_update_cs.hlsl

可視SurfaceProbeリストの要素を更新する.

#endif


#include "ssvg_util.hlsli"


#define RAY_SAMPLE_COUNT_PER_VOXEL 8
#define PROBE_UPDATE_TEMPORAL_RATE (0.025)

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // SurfaceProbeCellListを利用するバージョン.
    const uint visible_voxel_count = SurfaceProbeCellList[0]; // 0番目にアトミックカウンタが入っている.
    const uint update_element_index = (dtid.x * (WCP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(WCP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1)));
    
    if(visible_voxel_count < update_element_index)
        return;

    const uint voxel_index = SurfaceProbeCellList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
    // voxel_indexからtoroidal考慮したVoxelIDを計算する.
    int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.wcp.grid_resolution);
    int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.wcp.grid_resolution -cb_ssvg.wcp.grid_toroidal_offset, cb_ssvg.wcp.grid_resolution);


    // Cell中心.
    const float3 probe_cell_center = (float3(voxel_coord) + 0.5) * cb_ssvg.wcp.cell_size + cb_ssvg.wcp.grid_min_pos;
    #if 1
        // Probe埋まり回避.
        // RWWcpProbeBuffer[voxel_index].probe_offset_v3のuintからfloat3オフセット復元. Cellサイズの半分で正規化.
        float3 prev_probe_offset = decode_uint_to_range1_vec3(RWWcpProbeBuffer[voxel_index].probe_offset_v3) * (cb_ssvg.wcp.cell_size * 0.5);
        float3 probe_sample_pos_ws = probe_cell_center + prev_probe_offset;

        {
            const int relocation_count = 4;

            if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_ssvg.bbv.grid_resolution, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size_inv, probe_sample_pos_ws) != 0)
            {
                // 最大relocation_count回だけ埋まり回避のオフセットを試行する.
                for(int ri = 0; ri < relocation_count; ++ri)
                {
                    // Voxel内ランダムオフセットを加える.
                    const uint seed_0 = cb_ssvg.frame_count + ri;
                    const float3 random_offset = float3(noise_iqint32(float2(voxel_index, seed_0)), noise_iqint32(float2(update_element_index, seed_0)), noise_iqint32(float2(seed_0, voxel_index))) - 0.5;
                    probe_sample_pos_ws = probe_cell_center + random_offset * (cb_ssvg.wcp.cell_size * 0.4);// レンジはセルを超えない程度.

                    if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_ssvg.bbv.grid_resolution, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size_inv, probe_sample_pos_ws) == 0)
                    {
                        break;
                    }
                }
            }
        }
        // Probe位置更新. Cellサイズの半分で正規化.
        RWWcpProbeBuffer[voxel_index].probe_offset_v3 = encode_range1_vec3_to_uint(((probe_sample_pos_ws - probe_cell_center) / (cb_ssvg.wcp.cell_size * 0.5)));
    #else
        float3 probe_sample_pos_ws = probe_cell_center;
    #endif


    // Probeレイサンプル.
    {
        for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
        {
            // 全域Probe更新.
            // 球面Fibonacciシーケンス分布上をフルでトレースする.
            // 同時更新されるProbeのレイ方向がほとんど同じになるためか, Probe毎に乱数でサンプルするよりも数倍速くなる模様.
            const int num_fibonacci_point_max = 128;
            float3 sample_ray_dir = fibonacci_sphere_point((cb_ssvg.frame_count*RAY_SAMPLE_COUNT_PER_VOXEL + sample_index)%num_fibonacci_point_max, num_fibonacci_point_max);


            const float3 sample_ray_origin = probe_sample_pos_ws;            
                
            // SkyVisibility raycast.
            const float trace_distance = 100.0;
            int hit_voxel_index = -1;
            // リファクタリング版.
            float4 curr_ray_t_ws = trace_ray_vs_bitmask_brick_voxel_grid(
                hit_voxel_index,
                sample_ray_origin, sample_ray_dir, trace_distance, 
                cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
                cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

            // SkyVisibilityの方向平均を更新.
            const float sky_visibility = (0.0 > curr_ray_t_ws.x) ? 1.0 : 0.0;

             // ProbeOctMapの更新.
            const float2 octmap_uv = OctEncode(sample_ray_dir);
            const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.wcp.flatten_2d_width, voxel_index / cb_ssvg.wcp.flatten_2d_width);

            // 境界部込のテクセル位置.
            const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_probe_octmap_width_with_border + 1 + uint2(octmap_uv * k_probe_octmap_width);
            RWWcpProbeAtlasTex[octmap_atlas_texel_pos] = lerp(RWWcpProbeAtlasTex[octmap_atlas_texel_pos], sky_visibility, PROBE_UPDATE_TEMPORAL_RATE);
        }
    }

}

#if 0

wcp_element_update_cs.hlsl

全要素対象の更新処理.
高速化のためフレーム間で更新要素をスキップするロジックがある.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"


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

    const uint elem_count = cb_ssvg.wcp.grid_resolution.x * cb_ssvg.wcp.grid_resolution.y * cb_ssvg.wcp.grid_resolution.z;

    // 更新対象インデックスをフレーム毎のブロックに分けて採用する方式. こちらのほうがキャッシュ効率は有利なはず.
    const uint per_frame_loop_cnt = WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT+1;
    const uint per_frame_update_elem_count = (elem_count + (per_frame_loop_cnt - 1)) / per_frame_loop_cnt;
    const uint update_element_id = (((cb_ssvg.frame_count%per_frame_loop_cnt) * per_frame_update_elem_count)) + dtid.x;
    if(elem_count <= update_element_id)
        return;
    
    const int3 voxel_coord = index_to_voxel_coord(update_element_id, cb_ssvg.wcp.grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.wcp.grid_toroidal_offset, cb_ssvg.wcp.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.wcp.grid_resolution);

    // Cell中心.
    const float3 probe_cell_center = (float3(voxel_coord) + 0.5) * cb_ssvg.wcp.cell_size + cb_ssvg.wcp.grid_min_pos;

    // RWWcpProbeBuffer[voxel_index].probe_offset_v3のuintからfloat3オフセット復元. Cellサイズの半分で正規化.
    float3 prev_probe_offset = decode_uint_to_range1_vec3(RWWcpProbeBuffer[voxel_index].probe_offset_v3) * (cb_ssvg.wcp.cell_size * 0.5);
    float3 probe_sample_pos_ws = probe_cell_center + prev_probe_offset;


    // Probeレイサンプル.
    {
        float ray_accum = 0.0;

    #if 1 < RAY_SAMPLE_COUNT_PER_VOXEL
        for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
    #else
        const int sample_index = 0;
    #endif
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
            // レイキャスト.
            float4 curr_ray_t_ws = trace_ray_vs_bitmask_brick_voxel_grid(
                hit_voxel_index,
                sample_ray_origin, sample_ray_dir, trace_distance, 
                cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
                cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

            // SkyVisibilityの方向平均を更新.
            const float sky_visibility = (0.0 > curr_ray_t_ws.x) ? 1.0 : 0.0;

            ray_accum += sky_visibility;

            
             // ProbeOctMapの更新.
            const float2 octmap_uv = OctEncode(sample_ray_dir);
            const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.wcp.flatten_2d_width, voxel_index / cb_ssvg.wcp.flatten_2d_width);

            // 境界部込のテクセル位置.
            const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_probe_octmap_width_with_border + 1 + uint2(octmap_uv * k_probe_octmap_width);

            // ProbeAtlasTexel書き換え.
            RWWcpProbeAtlasTex[octmap_atlas_texel_pos] = lerp(RWWcpProbeAtlasTex[octmap_atlas_texel_pos], sky_visibility, PROBE_UPDATE_TEMPORAL_RATE);
        }

        // Probe要素の更新.
        RWWcpProbeBuffer[voxel_index].avg_sky_visibility = lerp(RWWcpProbeBuffer[voxel_index].avg_sky_visibility, ray_accum / RAY_SAMPLE_COUNT_PER_VOXEL, PROBE_UPDATE_TEMPORAL_RATE);
    }

}

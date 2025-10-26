
#if 0

wcp_probe_ray_sample.hlsli

#endif

#ifndef NGL_SHADER_SSVG_PROBE_SKY_VISIBILITY_SAMPLE_BASE_HLSLI
#define NGL_SHADER_SSVG_PROBE_SKY_VISIBILITY_SAMPLE_BASE_HLSLI

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"


// 可視Probeに対するIndirect処理の場合はこの定義をしてからIncludeする.
#if !defined(INDIRECT_MODE)
    #define INDIRECT_MODE 0
#endif

#if !defined(RAY_SAMPLE_COUNT_PER_VOXEL)
    // Probeの更新で発行するレイトレース数.
    #define RAY_SAMPLE_COUNT_PER_VOXEL 8
#endif


#if !defined(PROBE_UPDATE_TEMPORAL_RATE)
    #define PROBE_UPDATE_TEMPORAL_RATE  (0.1)
#endif



#if INDIRECT_MODE
    groupshared float2 shared_probe_octmap_accumulation[PROBE_UPDATE_THREAD_GROUP_SIZE * k_per_probe_texel_count];
#endif

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

    #if INDIRECT_MODE
        // VisibleCoarseVoxelListを利用するバージョン.
        const uint visible_voxel_count = VisibleVoxelList[0]; // 0番目にアトミックカウンタが入っている.
        const uint update_element_index = (dtid.x * (FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1)));
        if(visible_voxel_count < update_element_index)
            return;

        const uint voxel_index = VisibleVoxelList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
        // voxel_indexからtoroidal考慮したVoxelIDを計算する.
        int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.bbv.grid_resolution);
        int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution -cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);
        
        // Probeサンプリングワークをクリア.
        for(int i = 0; i < k_per_probe_texel_count; ++i)
        {
            shared_probe_octmap_accumulation[gindex * k_per_probe_texel_count + i] = float2(0.0, 0.0);
        }
    #else
        const uint voxel_count = cb_ssvg.bbv.grid_resolution.x * cb_ssvg.bbv.grid_resolution.y * cb_ssvg.bbv.grid_resolution.z;


        #if 0
            // 更新対象インデックスをn個飛ばしで採用する方式.
            const uint update_element_id = (dtid.x * (FRAME_UPDATE_ALL_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_ALL_PROBE_SKIP_COUNT+1)));
            if(voxel_count <= update_element_id)
                return;
        #else
            // 更新対象インデックスをフレーム毎のブロックに分けて採用する方式. こちらのほうがキャッシュ効率は有利なはず.
            const uint per_frame_loop_cnt = FRAME_UPDATE_ALL_PROBE_SKIP_COUNT+1;
            const uint per_frame_update_voxel_count = (voxel_count + (per_frame_loop_cnt - 1)) / per_frame_loop_cnt;
            const uint update_element_id = (((cb_ssvg.frame_count%per_frame_loop_cnt) * per_frame_update_voxel_count)) + dtid.x;
            if(voxel_count <= update_element_id)
                return;
        #endif

        const int3 voxel_coord = index_to_voxel_coord(update_element_id, cb_ssvg.bbv.grid_resolution);
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution);
    #endif


    const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);

    // 前パスで格納された線形インデックスからプローブ位置(レイ原点)を計算.
    BbvOptionalData voxel_optional_data = BitmaskBrickVoxelOptionData[voxel_index];
    // VoxelのMin位置.
    float3 probe_sample_pos_ws = float3(voxel_coord) * cb_ssvg.bbv.cell_size + cb_ssvg.bbv.grid_min_pos;
    if(0 < voxel_optional_data.probe_pos_code)
    {
        probe_sample_pos_ws += (float3(calc_bbv_bitcell_pos_from_bit_index(calc_bbv_probe_bitcell_index(voxel_optional_data))) + 0.5) * (cb_ssvg.bbv.cell_size / float(k_bbv_per_voxel_resolution));
    }
    else
    {
        // 占有されているセルが全て埋まっている場合はVoxel中心をプローブ位置にする.
        probe_sample_pos_ws += cb_ssvg.bbv.cell_size * 0.5;
    }

    // Probeレイサンプル.
    {
    #if 1 < RAY_SAMPLE_COUNT_PER_VOXEL
        for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
    #else
        const int sample_index = 0;
    #endif
        {
            #if INDIRECT_MODE
                // 可視Probe更新.
                // 完全ランダムな方向をサンプリング.
                float3 sample_ray_dir = random_unit_vector3(float2(voxel_index + sample_index, cb_ssvg.frame_count + sample_index));
            #else
                // 全域Probe更新.
                // 球面Fibonacciシーケンス分布上をフルでトレースする.
                // 同時更新されるProbeのレイ方向がほとんど同じになるためか, Probe毎に乱数でサンプルするよりも数倍速くなる模様.
                const int num_fibonacci_point_max = 128;
                float3 sample_ray_dir = fibonacci_sphere_point((cb_ssvg.frame_count*RAY_SAMPLE_COUNT_PER_VOXEL + sample_index)%num_fibonacci_point_max, num_fibonacci_point_max);
            #endif

            const float3 sample_ray_origin = probe_sample_pos_ws;            
                
            // SkyVisibility raycast.
            const float trace_distance = 10.0;
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
            const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.bbv.flatten_2d_width, voxel_index / cb_ssvg.bbv.flatten_2d_width);
            
            #if INDIRECT_MODE
                const uint2 octmap_texel_pos = uint2(octmap_uv * k_probe_octmap_width);
                // ワークにSharedMemを使用する.
                shared_probe_octmap_accumulation[gindex * k_per_probe_texel_count + (octmap_texel_pos.x + octmap_texel_pos.y * k_probe_octmap_width)] += float2(sky_visibility, 1.0);
            #else
                // 境界部込のテクセル位置.
                const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_probe_octmap_width_with_border + 1 + uint2(octmap_uv * k_probe_octmap_width);
                // TODO. 対応するAtlasTexの更新など.
                //RWAtlasTexTest[octmap_atlas_texel_pos] = lerp(RWAtlasTexTest[octmap_atlas_texel_pos], sky_visibility, PROBE_UPDATE_TEMPORAL_RATE);
            #endif
        }
    }


    #if INDIRECT_MODE
        // SharedMemの内容をバッファへ反映.
        for(int i = 0; i < k_per_probe_texel_count; ++i)
        {
            const int smi = gindex * k_per_probe_texel_count + i;
            // サンプルが無い場合は負数を設定しておく.
            const float sky_visibility = (0.0!=shared_probe_octmap_accumulation[smi].y)? (shared_probe_octmap_accumulation[smi].x / shared_probe_octmap_accumulation[smi].y) : -1.0;

            RWUpdateProbeWork[update_element_index * k_per_probe_texel_count + i] = sky_visibility;
        }
    #endif
}


#endif //NGL_SHADER_SSVG_PROBE_SKY_VISIBILITY_SAMPLE_BASE_HLSLI
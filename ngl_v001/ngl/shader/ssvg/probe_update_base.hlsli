
#if 0

probe_update_base.hlsli

// Probe更新の共通コード.
// Indirect版と非Indirect版で共通化.
// #define INDIRECT_MODE 定義をしてincludeすることでIndirect版になる.

#endif

#ifndef NGL_SHADER_SSVG_PROBE_UPDATE_BASE_HLSLI
#define NGL_SHADER_SSVG_PROBE_UPDATE_BASE_HLSLI


#define ENABLE_SHARED_MEMORY_ACCUM_OPTIMIZE


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

// DepthBufferに対してDispatch.
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
        const uint visible_voxel_count = VisibleCoarseVoxelList[0]; // 0番目にアトミックカウンタが入っている.
        const uint update_element_index = (dtid.x * (FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT+1)));
        if(visible_voxel_count < update_element_index)
            return;

        const uint voxel_index = VisibleCoarseVoxelList[update_element_index+1]; // 1番目以降に有効Voxelインデックスが入っている.
        // voxel_indexからtoroidal考慮したVoxelIDを計算する.
        int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_ssvg.base_grid_resolution);
        int3 voxel_coord = voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.base_grid_resolution -cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
        
        // Probeサンプリングワークをクリア.
        for(int i = 0; i < k_per_probe_texel_count; ++i)
        {
            shared_probe_octmap_accumulation[gindex * k_per_probe_texel_count + i] = float2(0.0, 0.0);
        }
    #else
        const uint voxel_count = cb_ssvg.base_grid_resolution.x * cb_ssvg.base_grid_resolution.y * cb_ssvg.base_grid_resolution.z;
        // 更新対象インデックスをスキップ.
        const uint update_element_id = (dtid.x * (FRAME_UPDATE_ALL_PROBE_SKIP_COUNT+1) + (cb_ssvg.frame_count%(FRAME_UPDATE_ALL_PROBE_SKIP_COUNT+1)));
        if(voxel_count <= update_element_id)
            return;

        const int3 voxel_coord = index_to_voxel_coord(update_element_id, cb_ssvg.base_grid_resolution);
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.base_grid_resolution);
    #endif


    const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
    const uint obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);

    // Probeの埋まり回避のための位置探索.
    // Obmセルを参照して空のセルから選択する.
    int candidate_probe_pos_bit_cell_index = -1;
    float candidate_probe_pos_dist_sq = 1e20;
    const float3 camera_pos_in_bit_cell_space = ((camera_pos - cb_ssvg.grid_min_pos) * cb_ssvg.cell_size_inv - float3(voxel_coord)) * float(k_obm_per_voxel_resolution);
    for(int i = 0; i < obm_voxel_occupancy_bitmask_uint_count(); ++i)
    {
        uint bit_block = (~OccupancyBitmaskVoxel[obm_addr + i]);

        for(int bi = 0; bi < 32 && 0 != bit_block; ++bi)
        {
            if(bit_block & 1)
            {
                const uint bit_index = i * 32 + bi;
                const uint3 bit_pos_in_voxel = calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(bit_index);
                
                // Voxel中心に近いセルを選択.
                const float3 score_vec = float3(bit_pos_in_voxel) - (float3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution) * 0.5);
                // カメラに一番近いセルを選択.
                //const float3 score_vec = float3(bit_pos_in_voxel) - camera_pos_in_bit_cell_space;

                const float dist_sq = dot(score_vec, score_vec);
                if(dist_sq < candidate_probe_pos_dist_sq)
                {
                    candidate_probe_pos_dist_sq = dist_sq;
                    candidate_probe_pos_bit_cell_index = bit_index;
                }
            }
            bit_block >>= 1;
        }
    }
    // VoxelのMin位置.
    float3 probe_sample_pos_ws = float3(voxel_coord) * cb_ssvg.cell_size + cb_ssvg.grid_min_pos;
    if(0 <= candidate_probe_pos_bit_cell_index)
    {
        probe_sample_pos_ws += (float3(calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(candidate_probe_pos_bit_cell_index)) + 0.5) * (cb_ssvg.cell_size / float(k_obm_per_voxel_resolution));
    }
    else
    {
        // 占有されているセルが全て埋まっている場合はVoxel中心をプローブ位置にする.
        probe_sample_pos_ws += cb_ssvg.cell_size * 0.5;
    }
    
    // CoarseVoxelの固有データ読み取り. 更新
    CoarseVoxelData coarse_voxel_data = RWCoarseVoxelBuffer[voxel_index];
    {
        // Probe配置位置をObmCellインデックスとして書き込み, 0が無効値であるようにして +1.
        coarse_voxel_data.probe_pos_index = (0 <= candidate_probe_pos_bit_cell_index) ? candidate_probe_pos_bit_cell_index+1 : 0;
    }
    // CoarseVoxelの固有データ書き込み.
    RWCoarseVoxelBuffer[voxel_index] = coarse_voxel_data;

    // Probeレイサンプル.
    {
    #if 1 < RAY_SAMPLE_COUNT_PER_VOXEL
        for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
    #else
        const int sample_index = 0;
    #endif
        {
            #if 1 < RAY_SAMPLE_COUNT_PER_VOXEL && 0
                // 球面Fibonacciシーケンス分布上をフルでトレースする.
                const uint sample_rand = noise_iqint32_orig(uint2(voxel_index, cb_ssvg.frame_count)) + sample_index;
                const int num_fibonacci_point_max = RAY_SAMPLE_COUNT_PER_VOXEL;
                float3 sample_ray_dir = fibonacci_sphere_point(sample_rand%num_fibonacci_point_max, num_fibonacci_point_max);
            #elif 1
                // 完全ランダムな方向をサンプリング.
                float3 sample_ray_dir = random_unit_vector3(float2(voxel_index + sample_index, cb_ssvg.frame_count + sample_index));
            #else
                // 球面Fibonacciシーケンス分布上でランダムな方向をサンプリング. あまり良くない.
                const uint sample_rand = noise_iqint32_orig(uint2(voxel_index + sample_index, cb_ssvg.frame_count));
                const int num_fibonacci_point_max = 256;
                float3 sample_ray_dir = fibonacci_sphere_point(sample_rand%num_fibonacci_point_max, num_fibonacci_point_max);
            #endif

            const float3 sample_ray_origin = probe_sample_pos_ws;            
                
            // SkyVisibility raycast.
            const float trace_distance = 10.0;
            int hit_voxel_index = -1;
            // リファクタリング版.
            float4 curr_ray_t_ws = trace_ray_vs_obm_voxel_grid(
                hit_voxel_index,
                sample_ray_origin, sample_ray_dir, trace_distance, 
                cb_ssvg.grid_min_pos, cb_ssvg.cell_size, cb_ssvg.base_grid_resolution,
                cb_ssvg.grid_toroidal_offset, OccupancyBitmaskVoxel);
                
            // SkyVisibilityの方向平均を更新.
            const float sky_visibility = (0.0 > curr_ray_t_ws.x) ? 1.0 : 0.0;
            // ProbeOctMapの更新.
            const float2 octmap_uv = OctEncode(sample_ray_dir);
            const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.probe_atlas_texture_base_width, voxel_index / cb_ssvg.probe_atlas_texture_base_width);
            
            #if INDIRECT_MODE
                const uint2 octmap_texel_pos = uint2(octmap_uv * k_probe_octmap_width);
                // ワークにSharedMemを使用する.
                shared_probe_octmap_accumulation[gindex * k_per_probe_texel_count + (octmap_texel_pos.x + octmap_texel_pos.y * k_probe_octmap_width)] += float2(sky_visibility, 1.0);
            #else
                // 境界部込のテクセル位置.
                const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_probe_octmap_width_with_border + 1 + uint2(octmap_uv * k_probe_octmap_width);
                RWTexProbeSkyVisibility[octmap_atlas_texel_pos] = lerp(RWTexProbeSkyVisibility[octmap_atlas_texel_pos], sky_visibility, PROBE_UPDATE_TEMPORAL_RATE);
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


#endif //NGL_SHADER_SSVG_PROBE_UPDATE_BASE_HLSLI
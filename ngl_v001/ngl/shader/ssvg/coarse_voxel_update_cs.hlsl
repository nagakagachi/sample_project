
#if 0

coarse_voxel_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"


#define RAY_SAMPLE_COUNT_PER_VOXEL 64
#define RAY_SAMPLE_CONTRIBUTE_COEF (1.0/4.0)

// Voxel更新のフレーム毎のスキップパラメータ. 1フレームに1/NのThreadGroupのVoxelだけを更新する.
#define FRAME_UPDATE_SKIP_THREAD_GROUP_COUNT 128


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
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

    const uint voxel_count = cb_ssvg.base_grid_resolution.x * cb_ssvg.base_grid_resolution.y * cb_ssvg.base_grid_resolution.z;
    
    // toroidalマッピング考慮.バッファインデックスに該当するVoxelは 3D座標->Toroidalマッピング->実インデックス で得る.
    const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_ssvg.base_grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.base_grid_resolution);

    if(voxel_count <= voxel_index)
        return;


    #if 1 < FRAME_UPDATE_SKIP_THREAD_GROUP_COUNT
    // フレーム毎の処理Voxelスキップ.
    if((gid.x % FRAME_UPDATE_SKIP_THREAD_GROUP_COUNT) != (cb_ssvg.frame_count % FRAME_UPDATE_SKIP_THREAD_GROUP_COUNT))
        return;
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

    // Probe配置位置をObmCellインデックスとして書き込み, 0が無効値であるようにして +1.
    coarse_voxel_data.probe_pos_index = (0 <= candidate_probe_pos_bit_cell_index) ? candidate_probe_pos_bit_cell_index+1 : 0;

    {
    #if 1 < RAY_SAMPLE_COUNT_PER_VOXEL
        for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
    #else
        const int sample_index = 0;
    #endif
        {
            #if 0
                const uint sample_rand = noise_iqint32_orig(uint2(voxel_index + sample_index, cb_ssvg.frame_count));
                // 球面Fibonacciシーケンス分布上でランダムな方向をサンプリング
                const int num_fibonacci_point_max = 256;
                float3 sample_ray_dir = fibonacci_sphere_point(sample_rand%num_fibonacci_point_max, num_fibonacci_point_max);
            #else
                // 完全ランダムな方向をサンプリング.
                float3 sample_ray_dir = random_unit_vector3(float2(voxel_index + sample_index, cb_ssvg.frame_count));
            #endif

            const float3 sample_ray_origin = probe_sample_pos_ws;            
                
            // SkyVisibility raycast.
            const float trace_distance = 100.0;
            int hit_voxel_index = -1;
            #if 1
                // リファクタリング版.
                float4 curr_ray_t_ws = trace_ray_vs_obm_voxel_grid(
                    hit_voxel_index,
                    sample_ray_origin, sample_ray_dir, trace_distance, 
                    cb_ssvg.grid_min_pos, cb_ssvg.cell_size, cb_ssvg.base_grid_resolution,
                    cb_ssvg.grid_toroidal_offset, OccupancyBitmaskVoxel);
            #else
                // 旧バージョン..
                float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
                    hit_voxel_index,
                    sample_ray_origin, sample_ray_dir, trace_distance, 
                    cb_ssvg.grid_min_pos, cb_ssvg.cell_size, cb_ssvg.base_grid_resolution,
                    cb_ssvg.grid_toroidal_offset, OccupancyBitmaskVoxel);
            #endif

            {
                // SkyVisibilityの方向平均を更新.
                const float sky_visibility = (0.0 > curr_ray_t_ws.x) ? 1.0 : 0.0;
                
                const int principal_axis = calc_principal_axis_component_index(abs(sample_ray_dir));
                int component_index = 0;
                // レイ方向から書き込み要素インデックスを計算 x:0,1 y:2,3 z:4,5
                component_index = (principal_axis * 2) + ((0 < sample_ray_dir[principal_axis]) ? 0 : 1);

                float next_avg = lerp(coarse_voxel_data.sky_visibility_dir_avg[component_index], sky_visibility, 0.1 * RAY_SAMPLE_CONTRIBUTE_COEF);
                // 安全な値にクランプ.
                next_avg = clamp(next_avg, 0.0, 1.0);
                coarse_voxel_data.sky_visibility_dir_avg[component_index] = next_avg;
            }
        }
    }

    // CoarseVoxelの固有データ書き込み.
    RWCoarseVoxelBuffer[voxel_index] = coarse_voxel_data;
}
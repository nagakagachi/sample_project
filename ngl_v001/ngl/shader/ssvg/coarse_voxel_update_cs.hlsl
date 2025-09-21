
#if 0

coarse_voxel_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

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

    const uint voxel_count = cb_dispatch_param.base_grid_resolution.x * cb_dispatch_param.base_grid_resolution.y * cb_dispatch_param.base_grid_resolution.z;
    
    // toroidalマッピング考慮.バッファインデックスに該当するVoxelは 3D座標->Toroidalマッピング->実インデックス で得る.
    const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.base_grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.grid_toroidal_offset, cb_dispatch_param.base_grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_dispatch_param.base_grid_resolution);


    if(voxel_index < voxel_count)
    {
        const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
        const uint obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);

        // Probeの埋まり回避のための位置探索.
        // Obmセルを参照して空のセルから選択する.
        int candidate_probe_pos_bit_cell_index = -1;
        float candidate_probe_pos_dist_sq = 1e20;
        const float3 camera_pos_in_bit_cell_space = ((camera_pos - cb_dispatch_param.grid_min_pos) * cb_dispatch_param.cell_size_inv - float3(voxel_coord)) * float(k_obm_per_voxel_resolution);
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
        float3 probe_sample_pos_ws = float3(voxel_coord) * cb_dispatch_param.cell_size + cb_dispatch_param.grid_min_pos;
        if(0 <= candidate_probe_pos_bit_cell_index)
        {
            probe_sample_pos_ws += (float3(calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(candidate_probe_pos_bit_cell_index)) + 0.5) * (cb_dispatch_param.cell_size / float(k_obm_per_voxel_resolution));
        }
        else
        {
            // 占有されているセルが全て埋まっている場合はVoxel中心をプローブ位置にする.
            probe_sample_pos_ws += cb_dispatch_param.cell_size * 0.5;
        }
            

        // 球面Fibonacciシーケンス分布上でランダムな方向をサンプリング
        const int num_fibonacci_point_max = 256;
        const uint sample_rand = noise_iqint32_orig(uint2(voxel_index, cb_dispatch_param.frame_count));
        float3 sample_ray_dir = fibonacci_sphere_point(sample_rand%num_fibonacci_point_max, num_fibonacci_point_max);
        const float3 sample_ray_origin = probe_sample_pos_ws;
           
        // SkyVisibility raycast.
        const float trace_distance = 10000.0;
        int hit_voxel_index = -1;
        float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
            hit_voxel_index,
            sample_ray_origin, sample_ray_dir, trace_distance, 
            cb_dispatch_param.grid_min_pos, cb_dispatch_param.cell_size, cb_dispatch_param.base_grid_resolution,
            cb_dispatch_param.grid_toroidal_offset, OccupancyBitmaskVoxel);

        // CoarseVoxelの固有データ読み取り. 更新
        {
            const uint2 coarse_voxel_data_code = RWCoarseVoxelBuffer[voxel_index];
            CoarseVoxelData coarse_voxel_data;
            coarse_voxel_decode(coarse_voxel_data, coarse_voxel_data_code);

            // SkyVisibilityをAccum.
            if(256 <= coarse_voxel_data.sample_count)
            {
                coarse_voxel_data.sample_count = coarse_voxel_data.sample_count/4;
                coarse_voxel_data.accumulated = coarse_voxel_data.accumulated/4;
            }
            coarse_voxel_data.sample_count += 1;
            if(0.0 > curr_ray_t_ws.x)
            {
                coarse_voxel_data.accumulated += 1;
            }

            // 0が無効値であるようにして +1 のbit cell index書き込み.
            coarse_voxel_data.probe_pos_index = (0 <= candidate_probe_pos_bit_cell_index) ? candidate_probe_pos_bit_cell_index+1 : 0;

            // CoarseVoxelの固有データ書き込み.
            RWCoarseVoxelBuffer[voxel_index] = coarse_voxel_encode(coarse_voxel_data);
        }
    }
}

#if 0

coarse_voxel_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

Buffer<uint>		OccupancyBitmaskVoxel;
RWBuffer<uint>		RWBufferWork;

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

    const uint voxel_count = cb_dispatch_param.BaseResolution.x * cb_dispatch_param.BaseResolution.y * cb_dispatch_param.BaseResolution.z;
    
    // toroidalマッピング考慮.バッファインデックスに該当するVoxelは 3D座標->Toroidalマッピング->実インデックス で得る.
    const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.BaseResolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);


    if(voxel_index < voxel_count)
    {
        const uint unique_data_addr = voxel_unique_data_addr(voxel_index);
        const uint obm_addr = voxel_occupancy_bitmask_data_addr(voxel_index);

        #if 1
            // 占有度が0のセルのうちで最も適切なものを選ぶ.条件は色々検討.
            int nearest_bit_cell_index = -1;
            float nearest_bit_cell_dist_sq = 1e20;
            const float3 camera_pos_in_bit_cell_space = ((camera_pos - cb_dispatch_param.GridMinPos) * cb_dispatch_param.CellSizeInv - float3(voxel_coord)) * float(k_per_voxel_occupancy_reso);
            for(int i = 0; i < voxel_occupancy_bitmask_uint_count(); ++i)
            {
                uint bit_block = (~OccupancyBitmaskVoxel[obm_addr + i]);

                for(int bi = 0; bi < 32 && 0 != bit_block; ++bi)
                {
                    if(bit_block & 1)
                    {
                        const uint bit_index = i * 32 + bi;
                        
                        const uint3 bit_pos_in_voxel = calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(bit_index);
                        
                        // カメラに一番近い空のセル.
                        const float3 score_vec = float3(bit_pos_in_voxel) - camera_pos_in_bit_cell_space;
                        // CoarseVoxel中心に近い空のセル.
                        //const float3 score_vec = float3(bit_pos_in_voxel) - (float3(k_per_voxel_occupancy_reso, k_per_voxel_occupancy_reso, k_per_voxel_occupancy_reso) * 0.5);

                        const float dist_sq = dot(score_vec, score_vec);
                        if(dist_sq < nearest_bit_cell_dist_sq)
                        {
                            nearest_bit_cell_dist_sq = dist_sq;
                            nearest_bit_cell_index = bit_index;
                        }
                    }
                    bit_block >>= 1;
                }
            }
            // VoxelのMin位置.
            float3 probe_sample_pos_ws = float3(voxel_coord) * cb_dispatch_param.CellSize + cb_dispatch_param.GridMinPos;
            if(0 <= nearest_bit_cell_index)
            {
                probe_sample_pos_ws += (float3(calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(nearest_bit_cell_index)) + 0.5) * (cb_dispatch_param.CellSize / float(k_per_voxel_occupancy_reso));
            }
            else
            {
                // 占有されているセルが全て埋まっている場合はVoxel中心をプローブ位置にする.
                probe_sample_pos_ws += cb_dispatch_param.CellSize * 0.5;
            }
                #if 1
                // デバッグ.
                // Voxel単位の色の見栄えのために高負荷だがカメラからレイを飛ばして遮蔽されている場合は外側にプローブを配置する.
                {
                    const float3 camera_to_probe = probe_sample_pos_ws - camera_pos;
                    const float3 ray_dir_ws = normalize(camera_to_probe);
                    // Voxelサイズの短距離トレースでカメラ遮蔽を確認.
                    int hit_voxel_index = -1;
                    float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
                        hit_voxel_index,
                        camera_pos, ray_dir_ws, dot(camera_to_probe, ray_dir_ws),
                        cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSize, cb_dispatch_param.BaseResolution,
                        cb_dispatch_param.GridToroidalOffset, OccupancyBitmaskVoxel);

                    if(0.0 <= curr_ray_t_ws.x)
                    {
                        // カメラに遮蔽されているカメラレイのヒット位置まで引き出す(距離があると遮蔽部がカメラに入ったときに明るい状態になるのでやり過ぎ).
                        probe_sample_pos_ws = camera_pos + ray_dir_ws * (curr_ray_t_ws.x - cb_dispatch_param.CellSize * 0.2);
                        // セル側からオフセット.
                        //probe_sample_pos_ws = probe_sample_pos_ws + ray_dir_ws * ( -cb_dispatch_param.CellSize * 4.0);
                    }
                }
                #endif


            float3 sample_ray_dir = random_unit_vector3(float2(probe_sample_pos_ws.x+probe_sample_pos_ws.y+probe_sample_pos_ws.z, (cb_dispatch_param.FrameCount)));
            const float3 sample_ray_origin = probe_sample_pos_ws;
        #else
            // Voxel中心からランダム方向にサンプリング.
            const float3 voxel_pos_ws = (float3(voxel_coord) + 0.5) * cb_dispatch_param.CellSize + cb_dispatch_param.GridMinPos;
            float3 sample_ray_dir = random_unit_vector3(float2(voxel_pos_ws.x+voxel_pos_ws.y+voxel_pos_ws.z, (cb_dispatch_param.FrameCount)));
            // 埋まり回避のためカメラ方向にオフセットしてみる.
            const float3 sample_ray_origin = voxel_pos_ws + normalize(camera_pos - voxel_pos_ws) * cb_dispatch_param.CellSize*1.1;
        #endif
        

        // 全球サンプリング.
        const float trace_distance = 10000.0;
        int hit_voxel_index = -1;
        float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
            hit_voxel_index,
            sample_ray_origin, sample_ray_dir, trace_distance, 
            cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSize, cb_dispatch_param.BaseResolution,
            cb_dispatch_param.GridToroidalOffset, OccupancyBitmaskVoxel);

        // CoarseVoxelの固有データ読み取り.
        const uint voxel_gi_data = RWBufferWork[voxel_index];
        uint voxel_gi_sample_count = voxel_gi_data & 0xFFFF;
        uint voxel_gi_accumulated = (voxel_gi_data >> 16) & 0xFFFF;
        // SkyVisibilityをAccum.
        if(512 <= voxel_gi_sample_count)
        {
            voxel_gi_sample_count = voxel_gi_sample_count/3;
            voxel_gi_accumulated = voxel_gi_accumulated/3;
        }
        voxel_gi_sample_count += 1;
        if(0.0 > curr_ray_t_ws.x)
        {
            voxel_gi_accumulated += 1;
        }

        // CoarseVoxelの固有データ書き込み.
        const uint new_voxel_gi_data = (clamp(voxel_gi_accumulated, 0, 65535) << 16) | clamp(voxel_gi_sample_count, 0, 65535);
        RWBufferWork[voxel_index] = new_voxel_gi_data;
        
    }
}
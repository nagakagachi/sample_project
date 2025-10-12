
#if 0

probe_common_update_cs.hlsl

// Probe更新.

#endif


#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

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


    const uint voxel_count = cb_ssvg.base_grid_resolution.x * cb_ssvg.base_grid_resolution.y * cb_ssvg.base_grid_resolution.z;
    // 動作検証のためこのシェーダはスキップなしの全体更新.
    const uint update_element_id = dtid.x;

    if(voxel_count <= update_element_id)
        return;

    const int3 voxel_coord = index_to_voxel_coord(update_element_id, cb_ssvg.base_grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.base_grid_resolution);


    const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
    const uint obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);

    ObmVoxelUniqueData unique_data;
    parse_obm_voxel_unique_data(unique_data, OccupancyBitmaskVoxel[unique_data_addr]);
    
    ObmVoxelOptionalData voxel_optional_data = RWObmVoxelOptionalBuffer[voxel_index];


    // Probe位置探索. 埋まり対策のために空Bitcell位置を探す.
    int candidate_probe_bitcell_index = -1;
    if(unique_data.is_occupied)
    {
        // Voxel内のProbe位置の更新.
        // Obmセルを参照して空のセルから選択する. Bitmaskが変化したVoxelだけ更新するようにしたいところ.
        float candidate_probe_pos_dist_sq = 1e20;
        const float3 camera_pos_in_bit_cell_space = ((camera_pos - cb_ssvg.grid_min_pos) * cb_ssvg.cell_size_inv - float3(voxel_coord)) * float(k_obm_per_voxel_resolution);
        for(int i = 0; i < obm_voxel_occupancy_bitmask_uint_count(); ++i)
        {
            // 0のbitcellを探す.
            uint bit_block = (~OccupancyBitmaskVoxel[obm_addr + i]);
            for(int bi = 0; bi < 32 && 0 != bit_block; ++bi)
            {
                if(bit_block & 1)
                {
                    const uint bit_index = i * 32 + bi;
                    const uint3 bitcell_pos_in_voxel = calc_obm_bitcell_pos_from_bit_index(bit_index);
                    
                    // Voxel中心に近いセルを選択.
                    const float3 score_vec = float3(bitcell_pos_in_voxel) - (float3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution) * 0.5);
                    // カメラに一番近いセルを選択.
                    //const float3 score_vec = float3(bitcell_pos_in_voxel) - camera_pos_in_bit_cell_space;

                    const float dist_sq = dot(score_vec, score_vec);
                    if(dist_sq < candidate_probe_pos_dist_sq)
                    {
                        candidate_probe_pos_dist_sq = dist_sq;
                        candidate_probe_bitcell_index = bit_index;
                    }
                }
                bit_block >>= 1;
            }
        }
    }

    // SurfaceDistance計算検証.
    // 実際にはマルチスレッド考慮せずに近傍情報参照しているため, 定常状態になるまでは一部正しくない距離情報が格納される場合がある近似処理に注意.
    int3 nearest_surface_dist = int3(1<<10, 1<<10, 1<<10);// 初期値は10bit範囲外としておく.
    {
        if(unique_data.is_occupied)
        {
            // 空ではないVoxelの場合は最近傍Surface情報をクリア.
            nearest_surface_dist = int3(0,0,0);
        }
        else
        {
            // 空Voxelの場合は以前の最近傍Surface情報を参照して更新.
            if(!all(0 == voxel_optional_data.surface_distance))
            {
                const int3 prev_nearest_voxel_coord = voxel_optional_data.surface_distance + voxel_coord;

                if(all(prev_nearest_voxel_coord >= 0) && all(prev_nearest_voxel_coord < cb_ssvg.base_grid_resolution))
                {
                    const int3 surface_voxel_coord_toroidal = voxel_coord_toroidal_mapping(prev_nearest_voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
                    const uint surface_voxel_obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_coord_to_index(surface_voxel_coord_toroidal, cb_ssvg.base_grid_resolution));

                    ObmVoxelUniqueData surface_unique_data;
                    parse_obm_voxel_unique_data(surface_unique_data, OccupancyBitmaskVoxel[surface_voxel_obm_addr]);
                    if(surface_unique_data.is_occupied)
                    {
                        // 現在も有効なVoxelなら有効なDistanceとして利用.
                        nearest_surface_dist = voxel_optional_data.surface_distance;
                    }
                }
            }
        }

        // 近傍Voxelを参照する更新.
        const int3 neighbor_offset[6] = {
            int3(-1,0,0), int3(1,0,0),
            int3(0,-1,0), int3(0,1,0),
            int3(0,0,-1), int3(0,0,1)
        };
        for(int i = 0; i < 6; ++i)
        {
            const int3 neighbor_voxel_coord = voxel_coord + neighbor_offset[i];
            if(all(neighbor_voxel_coord >= 0) && all(neighbor_voxel_coord < cb_ssvg.base_grid_resolution))
            {
                const int3 neighbor_voxel_coord_toroidal = voxel_coord_toroidal_mapping(neighbor_voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
                const uint neighbor_voxel_index = voxel_coord_to_index(neighbor_voxel_coord_toroidal, cb_ssvg.base_grid_resolution);
                
                const ObmVoxelOptionalData neighbor_voxel_optional_data = RWObmVoxelOptionalBuffer[neighbor_voxel_index];

                ObmVoxelUniqueData neighbor_unique_data;
                parse_obm_voxel_unique_data(neighbor_unique_data, OccupancyBitmaskVoxel[obm_voxel_unique_data_addr(neighbor_voxel_index)]);
                if(neighbor_unique_data.is_occupied)
                {
                    if(length_int_vector3(nearest_surface_dist) > length_int_vector3(neighbor_offset[i]))
                    {
                        nearest_surface_dist = neighbor_offset[i];
                    }
                }
                else
                {
                    if(!all(0 == neighbor_voxel_optional_data.surface_distance))
                    {
                        const int3 neighbor_surface_voxel_coord = neighbor_voxel_optional_data.surface_distance + neighbor_voxel_coord;
                        if(all(neighbor_surface_voxel_coord >= 0) && all(neighbor_surface_voxel_coord < cb_ssvg.base_grid_resolution))
                        {
                            if(length_int_vector3(nearest_surface_dist) > length_int_vector3(neighbor_surface_voxel_coord - voxel_coord))
                            {
                                // 一時無効化.
                                nearest_surface_dist = neighbor_surface_voxel_coord - voxel_coord;
                            }
                        }
                    }
                }
            }
        }

        uint new_surface_distance = ~0u;
        if(all(abs(nearest_surface_dist) < (1<<10)))
        {
            new_surface_distance = encode_10bit_int_vector3_to_u32(nearest_surface_dist);
        }
    }

    
    // Voxel追加データ更新
    {
        set_obm_probe_bitcell_index(voxel_optional_data, candidate_probe_bitcell_index);

        voxel_optional_data.surface_distance = nearest_surface_dist;
    }
    // Voxel追加データ書き込み.
    RWObmVoxelOptionalBuffer[voxel_index] = voxel_optional_data;

}
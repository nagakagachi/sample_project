
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


    // Voxel内のProbe位置の更新.
    // Obmセルを参照して空のセルから選択する.
    int candidate_probe_bitcell_index = -1;
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


    /*
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


                const uint neighbor_unique_data_addr = obm_voxel_unique_data_addr(neighbor_voxel_index);
                ObmVoxelOptionalData neighbor_coarse_voxel_data = CoarseVoxelBuffer[neighbor_voxel_index];

                ObmVoxelUniqueData neighbor_unique_data;
                parse_obm_voxel_unique_data(neighbor_unique_data, OccupancyBitmaskVoxel[neighbor_unique_data_addr]);
                if(neighbor_unique_data.is_occupied)
                {
                    // TODO.
                }
            }
        }
    */

    
    // CoarseVoxelの固有データ読み取り. 更新
    ObmVoxelOptionalData coarse_voxel_data = RWCoarseVoxelBuffer[voxel_index];
    {
        set_obm_probe_bitcell_index(coarse_voxel_data, candidate_probe_bitcell_index);
    }
    // CoarseVoxelの固有データ書き込み.
    RWCoarseVoxelBuffer[voxel_index] = coarse_voxel_data;

}
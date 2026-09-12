
#if 0

bbv_element_update_cs.hlsl

全要素対象の更新処理.
高速化のためフレーム間で更新要素をスキップするロジックがある.

#endif


#include "../instant_rdv_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;


[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{

    const uint elem_count = cb_instant_rdv.bbv.grid_resolution.x * cb_instant_rdv.bbv.grid_resolution.y * cb_instant_rdv.bbv.grid_resolution.z;

    // 更新対象インデックスをフレーム毎のブロックに分けて採用する方式. こちらのほうがキャッシュ効率は有利なはず.
    const uint per_frame_loop_cnt = BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT+1;
    const uint per_frame_update_elem_count = (elem_count + (per_frame_loop_cnt - 1)) / per_frame_loop_cnt;
    const uint update_element_id = (((cb_instant_rdv.frame_count%per_frame_loop_cnt) * per_frame_update_elem_count)) + dtid.x;
    if(elem_count <= update_element_id)
        return;


    const int3 voxel_coord =
        BbvMortonIndexToPhysicalVoxelCoord(update_element_id, cb_instant_rdv.bbv.grid_resolution);
    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_resolution);
    const uint voxel_index =
        BbvPhysicalVoxelCoordToMortonIndex(voxel_coord_toroidal, cb_instant_rdv.bbv.grid_resolution);

    
    BbvOptionalData voxel_optional_data = RWBitmaskBrickVoxelOptionData[voxel_index];


    // BBVにSDF的な距離情報を持たせる検証。Probe Relocationとは独立した実験である。
    // 表面Brickへの相対ベクトルを近傍伝播する処理は現在コメントアウトしている。
    // 更新パス自体は動作し、下の初期値をto_surface_vectorへ書くため、実距離は生成しない。
    // 実際にはマルチスレッド考慮せずに近傍情報参照しているため, 定常状態になるまでは一部正しくない距離情報が格納される場合がある近似処理に注意.
    int3 nearest_surface_dist = int3(1<<10, 1<<10, 1<<10);// 初期値は10bit範囲外としておく.
    /*
    {
        if(0 != bbv_occupied_flag)
        {
            // 空ではないVoxelの場合は最近傍Surface情報をクリア.
            nearest_surface_dist = int3(0,0,0);
        }
        else
        {
            // 空Voxelの場合は以前の最近傍Surface情報を参照して更新.
            if(!all(0 == voxel_optional_data.to_surface_vector))
            {
                const int3 prev_nearest_voxel_coord = voxel_optional_data.to_surface_vector + voxel_coord;

                if(all(prev_nearest_voxel_coord >= 0) && all(prev_nearest_voxel_coord < cb_instant_rdv.bbv.grid_resolution))
                {
                    const int3 surface_voxel_coord_toroidal = voxel_coord_toroidal_mapping(prev_nearest_voxel_coord, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_resolution);
                    const uint surface_occupied_voxel_count = BitmaskBrickVoxel[
                        bbv_voxel_coarse_occupancy_info_addr(
                            BbvPhysicalVoxelCoordToMortonIndex(
                                surface_voxel_coord_toroidal,
                                cb_instant_rdv.bbv.grid_resolution))];
                    if(0 != surface_occupied_voxel_count)
                    {
                        // 現在も有効なVoxelなら有効なDistanceとして利用.
                        nearest_surface_dist = voxel_optional_data.to_surface_vector;
                    }
                }
            }
        }

        // 近傍Voxel.
        const int3 neighbor_offset[6] = {
            int3(-1,0,0), int3(1,0,0),
            int3(0,-1,0), int3(0,1,0),
            int3(0,0,-1), int3(0,0,1)
        };
        for(int i = 0; i < 6; ++i)
        {
            const int3 neighbor_voxel_coord = voxel_coord + neighbor_offset[i];
            if(all(neighbor_voxel_coord >= 0) && all(neighbor_voxel_coord < cb_instant_rdv.bbv.grid_resolution))
            {
                const int3 neighbor_voxel_coord_toroidal = voxel_coord_toroidal_mapping(neighbor_voxel_coord, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_resolution);
                const uint neighbor_voxel_index = BbvPhysicalVoxelCoordToMortonIndex(
                    neighbor_voxel_coord_toroidal,
                    cb_instant_rdv.bbv.grid_resolution);
                
                const BbvOptionalData neighbor_voxel_optional_data = RWBitmaskBrickVoxelOptionData[neighbor_voxel_index];

                const uint neighbor_occupied_voxel_count = BitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(neighbor_voxel_index)];
                if(0 != neighbor_occupied_voxel_count)
                {
                    if(length_int_vector3(nearest_surface_dist) > length_int_vector3(neighbor_offset[i]))
                    {
                        nearest_surface_dist = neighbor_offset[i];
                    }
                }
                else
                {
                    if(!all(0 == neighbor_voxel_optional_data.to_surface_vector))
                    {
                        const int3 neighbor_surface_voxel_coord = neighbor_voxel_optional_data.to_surface_vector + neighbor_voxel_coord;
                        if(all(neighbor_surface_voxel_coord >= 0) && all(neighbor_surface_voxel_coord < cb_instant_rdv.bbv.grid_resolution))
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
    }
    */

    
    // Voxel追加データ更新.
    {
        voxel_optional_data.to_surface_vector = nearest_surface_dist;
    }
    // Voxel追加データ書き込み.
    RWBitmaskBrickVoxelOptionData[voxel_index] = voxel_optional_data;

}

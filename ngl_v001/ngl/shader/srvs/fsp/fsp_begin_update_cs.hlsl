
#if 0

fsp_begin_update_cs.hlsl

各種バッファクリアや, 移動によって発生した領域のInValidateをする.
Dispatchは全域としているが, 最適化としてはInvalidate領域サイズ分だけにしたい.

#endif


#include "../srvs_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

void FspPushFreeProbeIndex(uint probe_index)
{
    uint old_count = 0;
    InterlockedAdd(RWFspProbeFreeStack[0], 1, old_count);
    const uint write_index = old_count + 1;
    if(write_index <= cb_srvs.fsp_probe_pool_size)
    {
        RWFspProbeFreeStack[write_index] = probe_index;
    }
}

void FspClearProbeAtlas(uint probe_index)
{
    const uint2 probe_2d_map_pos = FspProbeAtlasMapPos(probe_index);
    [unroll]
    for(int oct_j = 0; oct_j < k_fsp_probe_octmap_width; ++oct_j)
    {
        [unroll]
        for(int oct_i = 0; oct_i < k_fsp_probe_octmap_width; ++oct_i)
        {
            RWFspProbeAtlasTex[probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)] = 0.0.xxxx;
        }
    }
}

// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    const uint probe_count = cb_srvs.fsp_total_cell_count;
    const uint probe_pool_size = cb_srvs.fsp_probe_pool_size;

    if(0 == dtid.x)
    {
        // アトミックカウンタをクリア. 0番目はアトミックカウンタ用に予約している.
        RWSurfaceProbeCellList[0] = 0;
        RWFspActiveProbeList[0] = 0;
        RWFspReleaseProbeList[0] = 0;
    }

    if(dtid.x < probe_pool_size)
    {
        RWFspProbePoolBuffer[dtid.x].flags &= ~k_fsp_probe_flag_visible_this_frame;
    }

    bool has_any_grid_move = false;
    [unroll]
    for(uint cascade_index = 0; cascade_index < k_fsp_max_cascade_count; ++cascade_index)
    {
        if(cascade_index >= FspCascadeCount())
        {
            break;
        }

        if(any(cb_srvs.fsp_cascade[cascade_index].grid.grid_move_cell_delta != int3(0,0,0)))
        {
            has_any_grid_move = true;
            break;
        }
    }

    if(!has_any_grid_move)
    {
        // 移動無しなら何もしない.
        return;
    }

    if(dtid.x < probe_count)
    {
        uint cascade_index = 0;
        uint local_cell_index = 0;
        if(!FspDecodeGlobalCellIndex(dtid.x, cascade_index, local_cell_index))
        {
            return;
        }

        const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
        int3 voxel_coord = index_to_voxel_coord(local_cell_index, cascade.grid.grid_resolution);
        // 移動によるInvalidateチェック..
        // バッファ上のVoxelアドレスをToroidalマッピング前の座標に変換. 修正版.
        int3 linear_voxel_coord = (voxel_coord - cascade.grid.grid_toroidal_offset_prev + cascade.grid.grid_resolution) % cascade.grid.grid_resolution;
        int3 voxel_coord_toroidal_curr = linear_voxel_coord - cascade.grid.grid_move_cell_delta;
        bool is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= (cascade.grid.grid_resolution));// 範囲外の領域に進行した場合はその領域をInvalidate.

        if(is_invalidate_area)
        {
            const uint old_probe_index = RWFspCellProbeIndexBuffer[dtid.x];
            if(old_probe_index != k_fsp_invalid_probe_index)
            {
                uint release_list_index = 0;
                InterlockedAdd(RWFspReleaseProbeList[0], 1, release_list_index);
                if(release_list_index < cb_srvs.fsp_release_probe_buffer_size)
                {
                    RWFspReleaseProbeList[release_list_index + 1] = old_probe_index;
                }

                FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[old_probe_index];
                probe_pool_data.owner_cell_index = k_fsp_invalid_probe_index;
                probe_pool_data.flags = 0;
                probe_pool_data.probe_offset_v3 = 0;
                probe_pool_data.avg_sky_visibility = 0.0;
                probe_pool_data.debug_last_released_frame = cb_srvs.frame_count;
                RWFspProbePoolBuffer[old_probe_index] = probe_pool_data;
                FspClearProbeAtlas(old_probe_index);

                FspPushFreeProbeIndex(old_probe_index);
            }

            // 移動によってシフトしてきた無効領域.
            RWFspProbeBuffer[dtid.x] = (FspProbeData)0;
            RWFspCellProbeIndexBuffer[dtid.x] = k_fsp_invalid_probe_index;
        }
    }
}

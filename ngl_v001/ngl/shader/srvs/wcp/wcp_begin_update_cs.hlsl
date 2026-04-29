
#if 0

wcp_begin_update_cs.hlsl

各種バッファクリアや, 移動によって発生した領域のInValidateをする.
Dispatchは全域としているが, 最適化としてはInvalidate領域サイズ分だけにしたい.

#endif


#include "../srvs_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

void WcpPushFreeProbeIndex(uint probe_index)
{
    uint old_count = 0;
    InterlockedAdd(RWWcpProbeFreeStack[0], 1, old_count);
    const uint write_index = old_count + 1;
    if(write_index <= cb_srvs.wcp_probe_pool_size)
    {
        RWWcpProbeFreeStack[write_index] = probe_index;
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
    uint probe_count = cb_srvs.wcp.grid_resolution.x * cb_srvs.wcp.grid_resolution.y * cb_srvs.wcp.grid_resolution.z;
    const uint probe_pool_size = cb_srvs.wcp_probe_pool_size;

    if(0 == dtid.x)
    {
        // アトミックカウンタをクリア. 0番目はアトミックカウンタ用に予約している.
        RWSurfaceProbeCellList[0] = 0;
        RWWcpActiveProbeList[0] = 0;
        RWWcpReleaseProbeList[0] = 0;
    }

    if(dtid.x < probe_pool_size)
    {
        RWWcpProbePoolBuffer[dtid.x].flags &= ~k_wcp_probe_flag_visible_this_frame;
    }

    if(all(cb_srvs.wcp.grid_move_cell_delta == int3(0,0,0)))
    {
        // 移動無しなら何もしない.
        return;
    }

    if(dtid.x < probe_count)
    {
        int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_srvs.wcp.grid_resolution);
        // 移動によるInvalidateチェック..
        // バッファ上のVoxelアドレスをToroidalマッピング前の座標に変換. 修正版.
        int3 linear_voxel_coord = (voxel_coord - cb_srvs.wcp.grid_toroidal_offset_prev + cb_srvs.wcp.grid_resolution) % cb_srvs.wcp.grid_resolution;
        int3 voxel_coord_toroidal_curr = linear_voxel_coord - cb_srvs.wcp.grid_move_cell_delta;
        bool is_invalidate_area = any(voxel_coord_toroidal_curr < 0) || any(voxel_coord_toroidal_curr >= (cb_srvs.wcp.grid_resolution));// 範囲外の領域に進行した場合はその領域をInvalidate.

        if(is_invalidate_area)
        {
            const uint old_probe_index = RWWcpCellProbeIndexBuffer[dtid.x];
            if(old_probe_index != k_wcp_invalid_probe_index)
            {
                uint release_list_index = 0;
                InterlockedAdd(RWWcpReleaseProbeList[0], 1, release_list_index);
                if(release_list_index < cb_srvs.wcp_release_probe_buffer_size)
                {
                    RWWcpReleaseProbeList[release_list_index + 1] = old_probe_index;
                }

                WcpProbePoolData probe_pool_data = RWWcpProbePoolBuffer[old_probe_index];
                probe_pool_data.owner_cell_index = k_wcp_invalid_probe_index;
                probe_pool_data.flags = 0;
                probe_pool_data.probe_offset_v3 = 0;
                probe_pool_data.avg_sky_visibility = 0.0;
                probe_pool_data.debug_last_released_frame = cb_srvs.frame_count;
                RWWcpProbePoolBuffer[old_probe_index] = probe_pool_data;

                WcpPushFreeProbeIndex(old_probe_index);
            }

            // 移動によってシフトしてきた無効領域.
            RWWcpProbeBuffer[dtid.x] = (WcpProbeData)0;
            RWWcpCellProbeIndexBuffer[dtid.x] = k_wcp_invalid_probe_index;
            
            {
                uint2 probe_2d_map_pos = uint2(dtid.x % cb_srvs.wcp.flatten_2d_width, dtid.x / cb_srvs.wcp.flatten_2d_width);
                for(int oct_j = 0; oct_j < k_wcp_probe_octmap_width_with_border; ++oct_j)
                {
                    for(int oct_i = 0; oct_i < k_wcp_probe_octmap_width_with_border; ++oct_i)
                    {
                        // ゼロクリア.
                        RWWcpProbeAtlasTex[probe_2d_map_pos * k_wcp_probe_octmap_width_with_border + uint2(oct_i, oct_j)] = 0.0;
                    }
                }
            }
        }
    }
}

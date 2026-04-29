
#if 0

wcp_clear_voxel_cs.hlsl

#endif


#include "../srvs_util.hlsli"


// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    uint probe_count = cb_srvs.wcp.grid_resolution.x * cb_srvs.wcp.grid_resolution.y * cb_srvs.wcp.grid_resolution.z;
    const uint probe_pool_size = cb_srvs.wcp_probe_pool_size;

    if(0 == dtid.x)
    {
        RWSurfaceProbeCellList[0] = 0;
        RWWcpProbeFreeStack[0] = probe_pool_size;
        RWWcpActiveProbeList[0] = 0;
        RWWcpReleaseProbeList[0] = 0;
    }

    if(dtid.x < probe_count)
    {
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

    if(dtid.x < probe_pool_size)
    {
        WcpProbePoolData probe_data = (WcpProbePoolData)0;
        probe_data.owner_cell_index = k_wcp_invalid_probe_index;
        RWWcpProbePoolBuffer[dtid.x] = probe_data;
        RWWcpProbeFreeStack[dtid.x + 1] = dtid.x;
    }
}

#if 0

wcp_element_update_cs.hlsl

V1 では coarse ray sample を止め、probe pool の stale release と
active probe list build に使う。

#endif

#include "../srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define WCP_STALE_FRAME_THRESHOLD (30u)

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

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    const uint probe_index = dtid.x;
    if(cb_srvs.wcp_probe_pool_size <= probe_index)
    {
        return;
    }

    WcpProbePoolData probe_pool_data = RWWcpProbePoolBuffer[probe_index];
    if(0 == (probe_pool_data.flags & k_wcp_probe_flag_allocated))
    {
        return;
    }

    const uint owner_cell_index = probe_pool_data.owner_cell_index;
    const bool has_valid_owner = (owner_cell_index != k_wcp_invalid_probe_index);
    const uint frame_age = cb_srvs.frame_count - probe_pool_data.last_seen_frame;
    const bool is_stale = has_valid_owner && (WCP_STALE_FRAME_THRESHOLD < frame_age);

    if(is_stale)
    {
        if(RWWcpCellProbeIndexBuffer[owner_cell_index] == probe_index)
        {
            RWWcpCellProbeIndexBuffer[owner_cell_index] = k_wcp_invalid_probe_index;
            RWWcpProbeBuffer[owner_cell_index] = (WcpProbeData)0;
        }

        uint release_list_index = 0;
        InterlockedAdd(RWWcpReleaseProbeList[0], 1, release_list_index);
        if(release_list_index < cb_srvs.wcp_release_probe_buffer_size)
        {
            RWWcpReleaseProbeList[release_list_index + 1] = probe_index;
        }

        probe_pool_data.owner_cell_index = k_wcp_invalid_probe_index;
        probe_pool_data.flags = 0;
        probe_pool_data.probe_offset_v3 = 0;
        probe_pool_data.avg_sky_visibility = 0.0;
        probe_pool_data.debug_last_released_frame = cb_srvs.frame_count;
        RWWcpProbePoolBuffer[probe_index] = probe_pool_data;

        WcpPushFreeProbeIndex(probe_index);
        return;
    }

    uint active_list_index = 0;
    InterlockedAdd(RWWcpActiveProbeList[0], 1, active_list_index);
    if(active_list_index < cb_srvs.wcp_active_probe_buffer_size)
    {
        RWWcpActiveProbeList[active_list_index + 1] = probe_index;
    }

    if(has_valid_owner)
    {
        RWWcpProbeBuffer[owner_cell_index].probe_offset_v3 = probe_pool_data.probe_offset_v3;
        RWWcpProbeBuffer[owner_cell_index].avg_sky_visibility = probe_pool_data.avg_sky_visibility;
    }
}

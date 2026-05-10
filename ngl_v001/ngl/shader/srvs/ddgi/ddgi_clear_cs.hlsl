/*
    ddgi_clear_cs.hlsl
    Clear dense DDGI probe buffers.
*/

#include "../srvs_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint total_sh_count = uint(max(cb_srvs.ddgi_total_cell_count, 0)) * 4;
    if(dtid.x < total_sh_count)
    {
        RWDdgiProbePackedShBuffer[dtid.x] = 0.0.xxxx;
    }

    const uint total_dist_count = uint(max(cb_srvs.ddgi_total_cell_count, 0)) * 8;
    if(dtid.x < total_dist_count)
    {
        RWDdgiProbeDistanceMomentBuffer[dtid.x] = 0.0.xxxx;
    }
}

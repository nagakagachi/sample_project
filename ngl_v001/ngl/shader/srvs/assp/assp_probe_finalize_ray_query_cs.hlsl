#if 0

assp_probe_finalize_ray_query_cs.hlsl

ASSP total ray count から Trace 用 DispatchIndirect 引数を生成する。

#endif

#include "assp_probe_common.hlsli"

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint total_ray_count = AsspProbeTotalRayCountBuffer[0];
    const uint dispatch_group_count_trace =
        (total_ray_count + (ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE - 1u)) / ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE;
    RWAsspProbeTraceIndirectArg[0] = max(dispatch_group_count_trace, 1u);
    RWAsspProbeTraceIndirectArg[1] = 1u;
    RWAsspProbeTraceIndirectArg[2] = 1u;
}

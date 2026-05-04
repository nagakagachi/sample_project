#include "assp_probe_common.hlsli"

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint representative_probe_count = AsspRepresentativeProbeList[0];
    const uint dispatch_group_count = (representative_probe_count + (ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP - 1u)) / ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP;
    RWAsspProbeIndirectArg[0] = max(dispatch_group_count, 1u);
    RWAsspProbeIndirectArg[1] = 1u;
    RWAsspProbeIndirectArg[2] = 1u;
}

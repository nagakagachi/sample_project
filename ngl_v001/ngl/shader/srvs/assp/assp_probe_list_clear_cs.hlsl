#include "assp_probe_common.hlsli"

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    RWAsspRepresentativeProbeList[0] = 0u;
}

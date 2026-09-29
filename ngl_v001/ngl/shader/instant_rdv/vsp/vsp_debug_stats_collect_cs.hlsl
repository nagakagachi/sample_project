#include "../instant_rdv_util.hlsli"

RWBuffer<uint> RWVspDebugStats;

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    RWVspDebugStats[0] = SurfaceProbeCellList[0];
    RWVspDebugStats[1] = VspProbeFreeStack[0];
    RWVspDebugStats[2] =
        VspActiveProbeListCurr[VspActiveProbeCurrentCounterSlot()];
}

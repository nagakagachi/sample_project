
#if 0

ss_probe_clear_cs.hlsl

ScreenSpaceProbe用テクスチャクリア.

#endif

#include "ssvg_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    RWScreenSpaceProbeTex[dtid.xy] = float4(0.0, 0.0, 0.0, 0.0);
}
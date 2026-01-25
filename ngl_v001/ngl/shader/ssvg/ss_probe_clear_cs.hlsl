
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
    // 8x8 texel per probe.
    // フル解像度でScreenSpaceProbeのタイル内スレッドに対応するのでdtid.xyでアクセス可能.
    RWScreenSpaceProbeTex[dtid.xy] = float4(0.0, 0.0, 0.0, 0.0);

    // Group内で代表スレッドが処理.
    if(all(gtid.xy == int2(0,0)))
    {
        // 1/8 Per ScreenSpaceProbe Tile Info Texture.
        // ThreadGroup毎に1Texelなのでgid.xyでアクセス可能.
        RWScreenSpaceProbeTileInfoTex[gid.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
}
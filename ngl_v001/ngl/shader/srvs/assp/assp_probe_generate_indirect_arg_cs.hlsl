#if 0

assp_probe_generate_indirect_arg_cs.hlsl

ASSP probe処理用のDispatchIndirect引数を生成し、
フレーム先頭のtotal ray counterを0クリアする。

#endif

#include "assp_probe_common.hlsli"

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    uint2 tile_info_size;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    const uint probe_count = tile_info_size.x * tile_info_size.y;
    const uint dispatch_group_count_probe =
        (probe_count + (ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP - 1u)) / ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP;
    RWAsspProbeIndirectArg[0] = max(dispatch_group_count_probe, 1u);
    RWAsspProbeIndirectArg[1] = 1u;
    RWAsspProbeIndirectArg[2] = 1u;
    RWAsspProbeTotalRayCountBuffer[0] = 0u;
}

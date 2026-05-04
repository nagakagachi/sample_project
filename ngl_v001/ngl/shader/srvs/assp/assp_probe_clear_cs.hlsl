#include "assp_probe_common.hlsli"

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 probe_tex_size;
    RWAdaptiveScreenSpaceProbeTex.GetDimensions(probe_tex_size.x, probe_tex_size.y);
    if(all(dtid.xy < probe_tex_size))
    {
        RWAdaptiveScreenSpaceProbeTex[dtid.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }

    if(all(gtid.xy == int2(0, 0)))
    {
        uint2 tile_info_size;
        RWAdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
        if(all(gid.xy < tile_info_size))
        {
            RWAdaptiveScreenSpaceProbeTileInfoTex[gid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        }
    }
}

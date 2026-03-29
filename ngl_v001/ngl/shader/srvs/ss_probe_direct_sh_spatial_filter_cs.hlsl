#if 0

ss_probe_direct_sh_spatial_filter_cs.hlsl

DirectSH方式 SkyVisibility に対するエッジアウェア空間フィルタ.

既存 ss_probe_spatial_filter_cs.hlsl の SH版.
SH テクスチャ(1/8解像度=TileInfo解像度)上で動作し,
1 スレッド = 1 タイル (1 SH テクセル).

近傍は上下左右4タイルのみ参照し, 法線・深度で重み付けしてSHをブレンドする.

Dispatch: DispatchHelper(sh_tex_width, sh_tex_height, 1) with numthreads(8,8,1).
          → dtid.xy が SH テクスチャの tile 座標に対応する.

#endif

#include "srvs_util.hlsli"

[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID)
{
    uint2 sh_tex_size;
    ScreenSpaceProbeDirectSHTex.GetDimensions(sh_tex_size.x, sh_tex_size.y);
    if (any(dtid.xy >= sh_tex_size))
        return;

    const int2 tile_id = int2(dtid.xy);
    const int2 tile_info_size = int2(sh_tex_size);

    const float4 center_value     = ScreenSpaceProbeDirectSHTex.Load(int3(tile_id, 0));
    const float4 center_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(tile_id, 0));
    if (!isValidDepth(center_tile_info.x))
    {
        RWScreenSpaceProbeDirectSHFilteredTex[tile_id] = center_value;
        return;
    }

    const float  center_depth  = center_tile_info.x;
    const float3 center_normal = normalize(OctDecode(center_tile_info.zw));

    float4 accum_value  = center_value;
    float  accum_weight = 1.0;

    const int2 neighbor_offsets[4] =
    {
        int2(-1, 0),
        int2( 1, 0),
        int2( 0,-1),
        int2( 0, 1)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 neighbor_tile = tile_id + neighbor_offsets[i];
        if (any(neighbor_tile < int2(0, 0)) || any(neighbor_tile >= tile_info_size))
            continue;

        const float4 neighbor_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(neighbor_tile, 0));
        if (!isValidDepth(neighbor_tile_info.x))
            continue;

        const float3 neighbor_normal = OctDecode(neighbor_tile_info.zw);
        const float  normal_dot      = dot(center_normal, neighbor_normal);
        if (normal_dot < cb_srvs.ss_probe_spatial_filter_normal_cos_threshold)
            continue;

        const float depth_diff    = abs(neighbor_tile_info.x - center_depth) / max(center_depth, 1e-4);
        const float depth_weight  = exp(-cb_srvs.ss_probe_spatial_filter_depth_exp_scale * depth_diff);

        const float4 neighbor_sh = ScreenSpaceProbeDirectSHTex.Load(int3(neighbor_tile, 0));
        accum_value  += neighbor_sh * depth_weight;
        accum_weight += depth_weight;
    }

    RWScreenSpaceProbeDirectSHFilteredTex[tile_id] = (accum_weight > 0.0) ? (accum_value / accum_weight) : center_value;
}

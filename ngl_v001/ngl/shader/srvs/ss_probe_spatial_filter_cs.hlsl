#if 0

ss_probe_spatial_filter_cs.hlsl

ScreenSpaceProbe OctMap(8x8)に対するシンプルなEdgeAware空間フィルタ.
近傍は上下左右のみ参照し、法線向きで棄却、深度差で重み付けしてブレンドする.

将来的にはMipによる有効Probe高速探索を利用した広範囲の空間フィルタも検討する.

#endif

#include "srvs_util.hlsli"

RWTexture2D<float4> RWScreenSpaceProbeFilteredTex;

[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 probe_tex_size;
    ScreenSpaceProbeTex.GetDimensions(probe_tex_size.x, probe_tex_size.y);
    if (any(dtid.xy >= probe_tex_size))
    {
        return;
    }

    uint2 tile_info_size;
    ScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);

    const int2 probe_tile_id = int2(gid.xy);
    if (any(probe_tile_id >= int2(tile_info_size)))
    {
        return;
    }

    const int2 local_cell = int2(gtid.xy);
    const int2 center_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE + local_cell;

    const float4 center_value = ScreenSpaceProbeTex.Load(int3(center_texel_pos, 0));
    const float4 center_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
    if (!isValidDepth(center_tile_info.x))
    {
        RWScreenSpaceProbeFilteredTex[center_texel_pos] = center_value;
        return;
    }

    const float center_depth = center_tile_info.x;
    const float3 center_normal = normalize(OctDecode(center_tile_info.zw));

    float4 accum_value = center_value;
    float accum_weight = 1.0;

    #if 0
        // デバッグ用.
        if((1920*0.5) < center_texel_pos.x)
        {
            RWScreenSpaceProbeFilteredTex[center_texel_pos] = accum_value;
            return;
        }
    #endif

    const int2 neighbor_offsets[4] =
    {
        int2(-1, 0),
        int2(1, 0),
        int2(0, -1),
        int2(0, 1)
    };

    // 隣接4タイルを走査してフィルタリング.
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 neighbor_tile = probe_tile_id + neighbor_offsets[i];
        if (any(neighbor_tile < int2(0, 0)) || any(neighbor_tile >= int2(tile_info_size)))
        {
            continue;
        }

        const float4 neighbor_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(neighbor_tile, 0));
        if (!isValidDepth(neighbor_tile_info.x))
        {
            continue;
        }

        const float3 neighbor_normal = OctDecode(neighbor_tile_info.zw);
        const float normal_dot = dot(center_normal, neighbor_normal);
        // 法線棄却.
        if (normal_dot < SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD)
        {
            continue;
        }

        const float depth_diff = abs(neighbor_tile_info.x - center_depth) / max(center_depth, 1e-4);
        const float depth_weight = exp(-SCREEN_SPACE_PROBE_SPATIAL_FILTER_DEPTH_EXP_SCALE * depth_diff);
        const float4 neighbor_value = ScreenSpaceProbeTex.Load(int3(neighbor_tile * SCREEN_SPACE_PROBE_TILE_SIZE + local_cell, 0));

        accum_value += neighbor_value * depth_weight;
        accum_weight += depth_weight;
    }

    RWScreenSpaceProbeFilteredTex[center_texel_pos] = (accum_weight > 0.0) ? (accum_value / accum_weight) : center_value;
}

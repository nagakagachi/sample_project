#if 0

assp_probe_preupdate_cs.hlsl

AdaptiveScreenSpaceProbe ProbeTile 用の前処理。
ASSP hierarchy を参照せず、全 4x4 tile で独立に probe 深度/位置/法線を更新する。

#endif

#include "assp_probe_common.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D TexHardwareDepth;

bool AsspTryLoadFrontDepthSample(
    int2 texel_pos,
    uint2 depth_size,
    out float probe_depth)
{
    probe_depth = 0.0;

    if(any(texel_pos < 0) || any(texel_pos >= int2(depth_size)))
        return false;

    const float depth = TexHardwareDepth.Load(int3(texel_pos, 0)).r;
    if(!isValidDepth(depth))
        return false;

    probe_depth = depth;
    return true;
}

float3 AsspCalcProbeNormalWs(int2 probe_texel_pos, float probe_depth, float2 depth_size_inv)
{
    const float3 probe_normal_vs = reconstruct_normal_vs_fine(
        TexHardwareDepth,
        probe_texel_pos,
        probe_depth,
        depth_size_inv,
        cb_ngl_sceneview.cb_ndc_z_to_view_z_coef,
        cb_ngl_sceneview.cb_proj_mtx);
    const float3 probe_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, probe_normal_vs);
    const float normal_len_sq = dot(probe_normal_ws, probe_normal_ws);
    return (normal_len_sq > 1e-8) ? (probe_normal_ws * rsqrt(normal_len_sq)) : float3(0.0, 0.0, 1.0);
}

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    uint2 tile_info_size;
    RWAdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    if(any(dtid.xy >= tile_info_size))
        return;

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);

    const int2 probe_id = int2(dtid.xy);
    const int2 tile_pixel_start = probe_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;

    uint2 probe_pos_in_tile = uint2(0, 0);
    int2 probe_texel_pos = tile_pixel_start;
    float probe_depth = 0.0;
    float front_linear_depth = 1e20;
    bool found_valid_probe = false;

    [unroll]
    for(int sy = 0; sy < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sy)
    {
        [unroll]
        for(int sx = 0; sx < ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE; ++sx)
        {
            const uint2 local_probe_pos = uint2(sx, sy);
            const int2 sample_texel_pos = tile_pixel_start + int2(local_probe_pos);

            float sample_depth = 0.0;
            if(!AsspTryLoadFrontDepthSample(sample_texel_pos, depth_size, sample_depth))
            {
                continue;
            }

            const float sample_linear_depth = abs(calc_view_z_from_ndc_z(sample_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef));
            if(sample_linear_depth < front_linear_depth)
            {
                front_linear_depth = sample_linear_depth;
                probe_pos_in_tile = local_probe_pos;
                probe_texel_pos = sample_texel_pos;
                probe_depth = sample_depth;
                found_valid_probe = true;
            }
        }
    }

    if(!found_valid_probe)
    {
        RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(1.0, uint2(0, 0), float2(0.0, 0.0), false);
        RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = 0xffffffffu;
        return;
    }

    const float3 probe_normal_ws = AsspCalcProbeNormalWs(probe_texel_pos, probe_depth, depth_size_inv);

    RWAdaptiveScreenSpaceProbeTileInfoTex[probe_id] = AsspTileInfoBuild(probe_depth, probe_pos_in_tile, OctEncode(probe_normal_ws), false);
    RWAdaptiveScreenSpaceProbeBestPrevTileTex[probe_id] = 0xffffffffu;
}

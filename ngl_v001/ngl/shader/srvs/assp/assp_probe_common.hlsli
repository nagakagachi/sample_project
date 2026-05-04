#ifndef NGL_SHADER_SRVS_ASSP_PROBE_COMMON_H
#define NGL_SHADER_SRVS_ASSP_PROBE_COMMON_H

#include "../srvs_util.hlsli"

Texture2D<float4>      AdaptiveScreenSpaceProbeTex;
Texture2D<float4>      AdaptiveScreenSpaceProbeHistoryTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbeTex;

Texture2D<float4>      AdaptiveScreenSpaceProbeTileInfoTex;
Texture2D<float4>      AdaptiveScreenSpaceProbeHistoryTileInfoTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbeTileInfoTex;

Texture2D<uint>        AdaptiveScreenSpaceProbeBestPrevTileTex;
RWTexture2D<uint>      RWAdaptiveScreenSpaceProbeBestPrevTileTex;

Texture2D<float4>      AdaptiveScreenSpaceProbePackedSHTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbePackedSHTex;

Buffer<uint>           AsspRepresentativeProbeList;
RWBuffer<uint>         RWAsspRepresentativeProbeList;
RWBuffer<uint>         RWAsspProbeIndirectArg;

static const uint k_assp_tile_info_probe_pos_mask = 0x3fu;
static const uint k_assp_tile_info_reprojection_succeeded_shift = 6u;
static const uint k_assp_tile_info_reprojection_succeeded_mask = (1u << k_assp_tile_info_reprojection_succeeded_shift);

uint AsspTileInfoYToPackedBits(float tile_info_y)
{
    return (uint)(tile_info_y + 0.5);
}

uint AsspTileInfoEncodeProbePosFlatIndex(uint2 probe_pos_in_tile)
{
    return probe_pos_in_tile.x + probe_pos_in_tile.y * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
}

uint AsspTileInfoDecodeProbePosFlatIndex(float tile_info_y)
{
    return AsspTileInfoYToPackedBits(tile_info_y) & k_assp_tile_info_probe_pos_mask;
}

int2 AsspTileInfoDecodeProbePosInTile(float tile_info_y)
{
    const uint flat_index = AsspTileInfoDecodeProbePosFlatIndex(tile_info_y);
    return int2(flat_index % ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE, flat_index / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
}

float AsspTileInfoBuildY(uint probe_pos_flat_index, bool is_reprojection_succeeded)
{
    uint packed = probe_pos_flat_index & k_assp_tile_info_probe_pos_mask;
    if(is_reprojection_succeeded)
    {
        packed |= k_assp_tile_info_reprojection_succeeded_mask;
    }
    return (float)packed;
}

float AsspTileInfoBuildY(uint2 probe_pos_in_tile, bool is_reprojection_succeeded)
{
    return AsspTileInfoBuildY(AsspTileInfoEncodeProbePosFlatIndex(probe_pos_in_tile), is_reprojection_succeeded);
}

float4 AsspTileInfoBuild(float depth, uint2 probe_pos_in_tile, float2 approx_normal_oct, bool is_reprojection_succeeded)
{
    return float4(depth, AsspTileInfoBuildY(probe_pos_in_tile, is_reprojection_succeeded), approx_normal_oct.x, approx_normal_oct.y);
}

uint AsspPackProbeTileId(uint2 probe_tile_id)
{
    return (probe_tile_id.x & 0xffffu) | ((probe_tile_id.y & 0xffffu) << 16u);
}

int2 AsspUnpackProbeTileId(uint packed_probe_tile_id)
{
    return int2(packed_probe_tile_id & 0xffffu, packed_probe_tile_id >> 16u);
}

int2 AsspPackedShAtlasCoeffOffset(uint coeff_index, int2 logical_resolution)
{
    switch(coeff_index)
    {
    default:
    case 0: return int2(0, 0);
    case 1: return int2(logical_resolution.x, 0);
    case 2: return int2(0, logical_resolution.y);
    case 3: return int2(logical_resolution.x, logical_resolution.y);
    }
}

int2 AsspPackedShAtlasTexelCoord(int2 probe_tile_id, uint coeff_index, int2 logical_resolution)
{
    return probe_tile_id + AsspPackedShAtlasCoeffOffset(coeff_index, logical_resolution);
}

#endif

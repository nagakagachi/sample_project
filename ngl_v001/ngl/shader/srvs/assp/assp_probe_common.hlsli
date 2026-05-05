#ifndef NGL_SHADER_SRVS_ASSP_PROBE_COMMON_H
#define NGL_SHADER_SRVS_ASSP_PROBE_COMMON_H

#include "../srvs_util.hlsli"
#include "assp_buffer_util.hlsli"

Texture2D<float4>      AdaptiveScreenSpaceProbeTex;
Texture2D<float4>      AdaptiveScreenSpaceProbeHistoryTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbeTex;

Texture2D<float4>      AdaptiveScreenSpaceProbeVarianceTex;
Texture2D<float4>      AdaptiveScreenSpaceProbeHistoryVarianceTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbeVarianceTex;

Texture2D<float4>      AdaptiveScreenSpaceProbeTileInfoTex;
Texture2D<float4>      AdaptiveScreenSpaceProbeHistoryTileInfoTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbeTileInfoTex;

Texture2D<uint>        AdaptiveScreenSpaceProbeRepresentativeTileTex;
Texture2D<uint>        AdaptiveScreenSpaceProbeHistoryRepresentativeTileTex;
RWTexture2D<uint>      RWAdaptiveScreenSpaceProbeRepresentativeTileTex;

Texture2D<uint>        AdaptiveScreenSpaceProbeBestPrevTileTex;
RWTexture2D<uint>      RWAdaptiveScreenSpaceProbeBestPrevTileTex;

Texture2D<float4>      AdaptiveScreenSpaceProbePackedSHTex;
RWTexture2D<float4>    RWAdaptiveScreenSpaceProbePackedSHTex;

Buffer<uint>           AsspRepresentativeProbeList;
RWBuffer<uint>         RWAsspRepresentativeProbeList;
RWBuffer<uint>         RWAsspProbeIndirectArg;
RWBuffer<uint>         RWAsspProbeTraceIndirectArg;
Buffer<uint>           AsspProbeTotalRayCountBuffer;
RWBuffer<uint>         RWAsspProbeTotalRayCountBuffer;
Buffer<uint>           AsspProbeRayMetaBuffer;
RWBuffer<uint>         RWAsspProbeRayMetaBuffer;
Buffer<uint>           AsspProbeRayQueryBuffer;
RWBuffer<uint>         RWAsspProbeRayQueryBuffer;
Buffer<uint>           AsspProbeRayResultBuffer;
RWBuffer<uint>         RWAsspProbeRayResultBuffer;

static const uint k_assp_ray_count_max = ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
static const uint k_assp_ray_meta_count_bits = 5u;
static const uint k_assp_ray_meta_count_mask = (1u << k_assp_ray_meta_count_bits) - 1u;
static const uint k_assp_ray_meta_offset_shift = k_assp_ray_meta_count_bits;
static const uint k_assp_ray_query_local_ray_bits = 5u;
static const uint k_assp_ray_query_local_ray_mask = (1u << k_assp_ray_query_local_ray_bits) - 1u;
static const uint k_assp_ray_query_probe_index_shift = k_assp_ray_query_local_ray_bits;

uint AsspPackRayMeta(uint ray_offset, uint ray_count)
{
    return (ray_offset << k_assp_ray_meta_offset_shift) | (ray_count & k_assp_ray_meta_count_mask);
}

uint AsspUnpackRayMetaCount(uint packed_meta)
{
    return packed_meta & k_assp_ray_meta_count_mask;
}

uint AsspUnpackRayMetaOffset(uint packed_meta)
{
    return packed_meta >> k_assp_ray_meta_offset_shift;
}

uint AsspPackRayQuery(uint probe_list_index, uint local_ray_index)
{
    return (probe_list_index << k_assp_ray_query_probe_index_shift) | (local_ray_index & k_assp_ray_query_local_ray_mask);
}

uint AsspUnpackRayQueryProbeListIndex(uint packed_query)
{
    return packed_query >> k_assp_ray_query_probe_index_shift;
}

uint AsspUnpackRayQueryLocalRayIndex(uint packed_query)
{
    return packed_query & k_assp_ray_query_local_ray_mask;
}

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

uint AsspProbeTileCount()
{
    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    return tile_info_size_u32.x * tile_info_size_u32.y;
}

bool AsspTryGetProbeTileIdFromLinearIndex(uint probe_linear_index, out int2 probe_tile_id)
{
    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    if(0u == tile_info_size_u32.x || 0u == tile_info_size_u32.y)
    {
        probe_tile_id = int2(-1, -1);
        return false;
    }

    const uint probe_tile_count = tile_info_size_u32.x * tile_info_size_u32.y;
    if(probe_linear_index >= probe_tile_count)
    {
        probe_tile_id = int2(-1, -1);
        return false;
    }

    probe_tile_id = int2(probe_linear_index % tile_info_size_u32.x, probe_linear_index / tile_info_size_u32.x);
    return true;
}

bool AsspTryGetProbeLinearIndexFromTileId(int2 probe_tile_id, out uint probe_linear_index)
{
    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    if(any(probe_tile_id < int2(0, 0)) || any(probe_tile_id >= int2(tile_info_size_u32)))
    {
        probe_linear_index = 0u;
        return false;
    }

    probe_linear_index = uint(probe_tile_id.y) * tile_info_size_u32.x + uint(probe_tile_id.x);
    return true;
}

void AsspResolveLeafNodeFromCurrentRepresentativeTileMap(
    int2 screen_texel_pos,
    out int selected_lod,
    out int2 selected_node_origin,
    out int selected_node_size,
    out int2 representative_texel_pos,
    out float representative_depth,
    out float plane_error,
    out float split_score,
    out float3 representative_normal)
{
    selected_lod = 0;
    selected_node_origin = int2(0, 0);
    selected_node_size = int(k_assp_tile_size);
    representative_texel_pos = int2(-1, -1);
    representative_depth = 0.0;
    plane_error = 0.0;
    split_score = 0.0;
    representative_normal = float3(0.0, 0.0, 1.0);

    const int2 current_tile_id = screen_texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    if(any(current_tile_id < 0) || any(current_tile_id >= int2(tile_info_size_u32)))
    {
        return;
    }

    const float4 tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(current_tile_id, 0));
    if(!isValidDepth(tile_info.x))
    {
        return;
    }

    selected_lod = 0;
    selected_node_origin = current_tile_id * int(ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
    selected_node_size = int(ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
    representative_depth = tile_info.x;
    representative_normal = normalize(OctDecode(tile_info.zw));
    representative_texel_pos = current_tile_id * int(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION) + AsspTileInfoDecodeProbePosInTile(tile_info.y);
    plane_error = 0.0;
    split_score = 0.0;
}

bool AsspTryResolveRepresentativeTileIdFromCurrentRepresentativeTileMap(
    int2 screen_texel_pos,
    out int2 representative_tile_id,
    out int selected_lod,
    out int2 selected_node_origin,
    out int selected_node_size)
{
    int2 representative_texel_pos;
    float representative_depth;
    float plane_error;
    float split_score;
    float3 representative_normal;
    AsspResolveLeafNodeFromCurrentRepresentativeTileMap(
        screen_texel_pos,
        selected_lod,
        selected_node_origin,
        selected_node_size,
        representative_texel_pos,
        representative_depth,
        plane_error,
        split_score,
        representative_normal);

    representative_tile_id = int2(-1, -1);
    const int2 current_tile_id = screen_texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    if(any(current_tile_id < int2(0, 0)) || any(current_tile_id >= int2(tile_info_size_u32)))
    {
        return false;
    }

    const float4 tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(current_tile_id, 0));
    if(!isValidDepth(tile_info.x))
    {
        return false;
    }

    representative_tile_id = current_tile_id;
    return any(representative_texel_pos >= int2(0, 0));
}

bool AsspTryResolveSpatialFilterNeighborRepresentativeTileId(
    int2 center_representative_tile_id,
    int2 direction,
    out int2 neighbor_representative_tile_id)
{
    neighbor_representative_tile_id = int2(-1, -1);

    uint2 tile_info_size_u32;
    AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
    const int2 tile_info_size = int2(tile_info_size_u32);
    const int2 neighbor_tile = center_representative_tile_id + direction;
    if(any(neighbor_tile < int2(0, 0)) || any(neighbor_tile >= tile_info_size))
    {
        return false;
    }

    const float4 neighbor_tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(neighbor_tile, 0));
    if(!isValidDepth(neighbor_tile_info.x))
    {
        return false;
    }

    neighbor_representative_tile_id = neighbor_tile;
    return true;
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

int2 AsspPackedShAtlasLogicalResolution()
{
    uint2 packed_sh_tex_size;
    AdaptiveScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    return int2(packed_sh_tex_size >> 1);
}

float4 AsspPackedShAtlasLoadCoeff(int2 probe_tile_id, uint coeff_index)
{
    const int2 logical_resolution = AsspPackedShAtlasLogicalResolution();
    return AdaptiveScreenSpaceProbePackedSHTex.Load(int3(AsspPackedShAtlasTexelCoord(probe_tile_id, coeff_index, logical_resolution), 0));
}

#endif

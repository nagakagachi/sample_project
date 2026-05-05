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

    uint2 representative_tile_tex_size;
    AdaptiveScreenSpaceProbeRepresentativeTileTex.GetDimensions(representative_tile_tex_size.x, representative_tile_tex_size.y);
    const int2 current_tile_id = screen_texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    if(any(current_tile_id < 0) || any(current_tile_id >= int2(representative_tile_tex_size)))
    {
        return;
    }

    const uint representative_tile_packed = AdaptiveScreenSpaceProbeRepresentativeTileTex.Load(int3(current_tile_id, 0)).x;
    if(0xffffffffu == representative_tile_packed)
    {
        return;
    }

    const int2 representative_tile_id = AsspUnpackProbeTileId(representative_tile_packed);
    const AsspLod0NodeRecord representative_lod0_node = AsspLoadLod0Node(uint2(representative_tile_id));
    representative_texel_pos = AsspUnpackRepresentativeTexelInt2(representative_lod0_node.representative_texel_packed);
    representative_depth = representative_lod0_node.front_depth;
    plane_error = representative_lod0_node.plane_error;
    split_score = representative_lod0_node.split_score;
    representative_normal = AsspLod0NodeIsSolid(representative_lod0_node) ? representative_lod0_node.representative_normal : float3(0.0, 0.0, 1.0);
    selected_node_origin = representative_tile_id * int(ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);

    const int2 coarse_tile_origin = (current_tile_id / 2) * 2;
    bool is_same_representative_in_coarse_block = true;
    [unroll]
    for(int oy = 0; oy < 2; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < 2; ++ox)
        {
            const int2 block_tile_id = coarse_tile_origin + int2(ox, oy);
            if(any(block_tile_id >= int2(representative_tile_tex_size)))
            {
                continue;
            }

            const uint block_representative_tile_packed = AdaptiveScreenSpaceProbeRepresentativeTileTex.Load(int3(block_tile_id, 0)).x;
            if(block_representative_tile_packed != representative_tile_packed)
            {
                is_same_representative_in_coarse_block = false;
            }
        }
    }

    if(is_same_representative_in_coarse_block)
    {
        selected_lod = 1;
        selected_node_origin = coarse_tile_origin * int(ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
        selected_node_size = int(AsspNodeSizeInPixels(1u));
    }
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
    if(any(representative_texel_pos < 0))
    {
        return false;
    }

    representative_tile_id = representative_texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    return true;
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

    int selected_lod;
    int2 selected_node_origin;
    int selected_node_size;
    int2 resolved_center_representative_tile_id;
    if(!AsspTryResolveRepresentativeTileIdFromCurrentRepresentativeTileMap(
        center_representative_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE,
        resolved_center_representative_tile_id,
        selected_lod,
        selected_node_origin,
        selected_node_size))
    {
        return false;
    }

    // LOD0 は tile 隣接がそのまま side-neighbor になるので direct 解決でよい。
    // LOD1 は representative child の位置に依存すると方向ごとの参照先が不安定になるため、
    // coarse node 境界基準で近傍を解決する。
    if(0 == selected_lod)
    {
        const int2 direct_lod0_neighbor_tile = center_representative_tile_id + direction;
        if(all(direct_lod0_neighbor_tile >= int2(0, 0)) && all(direct_lod0_neighbor_tile < tile_info_size))
        {
            const float4 direct_lod0_neighbor_tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(direct_lod0_neighbor_tile, 0));
            if(isValidDepth(direct_lod0_neighbor_tile_info.x))
            {
                neighbor_representative_tile_id = direct_lod0_neighbor_tile;
                return true;
            }
        }
    }

    // LOD1、あるいは LOD0 direct が見つからない場合は、自身が属する current node の境界外サンプルから
    // representative map 経由で「その方向の side-neighbor node」を解決する。
    const int2 node_center = selected_node_origin + int2(selected_node_size / 2, selected_node_size / 2);
    int2 neighbor_sample_screen_texel = node_center;
    if(direction.x < 0)
    {
        neighbor_sample_screen_texel = int2(selected_node_origin.x - 1, node_center.y);
    }
    else if(direction.x > 0)
    {
        neighbor_sample_screen_texel = int2(selected_node_origin.x + selected_node_size, node_center.y);
    }
    else if(direction.y < 0)
    {
        neighbor_sample_screen_texel = int2(node_center.x, selected_node_origin.y - 1);
    }
    else if(direction.y > 0)
    {
        neighbor_sample_screen_texel = int2(node_center.x, selected_node_origin.y + selected_node_size);
    }

    const int2 screen_size = int2(AsspScreenWidth(), AsspScreenHeight());
    if(any(neighbor_sample_screen_texel < int2(0, 0)) || any(neighbor_sample_screen_texel >= screen_size))
    {
        return false;
    }

    int neighbor_lod;
    int2 neighbor_node_origin;
    int neighbor_node_size;
    if(!AsspTryResolveRepresentativeTileIdFromCurrentRepresentativeTileMap(
        neighbor_sample_screen_texel,
        neighbor_representative_tile_id,
        neighbor_lod,
        neighbor_node_origin,
        neighbor_node_size))
    {
        return false;
    }

    if(all(neighbor_representative_tile_id == resolved_center_representative_tile_id))
    {
        neighbor_representative_tile_id = int2(-1, -1);
        return false;
    }

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

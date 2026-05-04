#ifndef NGL_SHADER_SRVS_ASSP_BUFFER_UTIL_H
#define NGL_SHADER_SRVS_ASSP_BUFFER_UTIL_H

struct AsspNodeRecord
{
    // state:
    //   invalid = 画面端のはみ出しなどで node 自体が存在しない
    //   empty   = node は存在するが深度サンプルがなく空領域
    //   solid   = representative を持つ有効 node
    uint state;
    // LOD1+ でも representative plane を直接持たず、最終的な plane/normal 参照は LOD0 へ寄せる。
    // これで上位 LOD の record を軽く保ちつつ、debug や評価時は代表 texel から LOD0 を引き直せる。
    uint2 representative_texel;
    float front_depth;
    float3 representative_normal;
    float representative_plane_dist;
    // metric0..2 は LOD ごとに意味を切り替える。
    // LOD0   : mean_residual / max_residual / depth_range
    // LOD1+  : max_residual / max_behind_gap / reserved
    float metric0;
    float metric1;
    float metric2;
    float split_score;
};

uint AsspScreenWidth()
{
    return (uint)max(cb_srvs.tex_main_view_depth_size.x, 1);
}

uint AsspScreenHeight()
{
    return (uint)max(cb_srvs.tex_main_view_depth_size.y, 1);
}

uint AsspLodWidthFromCb(uint lod_index)
{
    return AsspLodWidth(AsspScreenWidth(), lod_index);
}

uint AsspLodHeightFromCb(uint lod_index)
{
    return AsspLodHeight(AsspScreenHeight(), lod_index);
}

uint AsspLodBaseWordOffsetFromCb(uint lod_index)
{
    return AsspLodBaseWordOffset(AsspScreenWidth(), AsspScreenHeight(), lod_index);
}

uint AsspNodeWordOffset(uint lod_index, uint2 node_coord)
{
    return AsspLodBaseWordOffsetFromCb(lod_index) + (node_coord.y * AsspLodWidthFromCb(lod_index) + node_coord.x) * (uint)cb_srvs.assp_words_per_node;
}

bool AsspIsNodeCoordInRange(uint lod_index, uint2 node_coord)
{
    return (node_coord.x < AsspLodWidthFromCb(lod_index)) && (node_coord.y < AsspLodHeightFromCb(lod_index));
}

uint2 AsspNodeCoordFromScreenTexel(int2 screen_texel_pos, uint lod_index)
{
    return uint2(screen_texel_pos) / AsspNodeSizeInPixels(lod_index);
}

int2 AsspNodeOriginFromCoord(uint2 node_coord, uint lod_index)
{
    return int2(node_coord * AsspNodeSizeInPixels(lod_index));
}

AsspNodeRecord AsspMakeInvalidNode()
{
    AsspNodeRecord node;
    node.state = k_assp_state_invalid;
    node.representative_texel = uint2(~0u, ~0u);
    node.front_depth = -1.0;
    node.representative_normal = float3(0.0, 0.0, 1.0);
    node.representative_plane_dist = 0.0;
    node.metric0 = 0.0;
    node.metric1 = 0.0;
    node.metric2 = 0.0;
    node.split_score = 0.0;
    return node;
}

AsspNodeRecord AsspMakeEmptyNode()
{
    AsspNodeRecord node = AsspMakeInvalidNode();
    node.state = k_assp_state_empty;
    node.front_depth = 0.0;
    return node;
}

bool AsspNodeIsValid(AsspNodeRecord node)
{
    return node.state != k_assp_state_invalid;
}

bool AsspNodeIsSolid(AsspNodeRecord node)
{
    return node.state == k_assp_state_solid;
}

bool AsspNodeIsEmpty(AsspNodeRecord node)
{
    return node.state == k_assp_state_empty;
}

float4 AsspNodePlane(AsspNodeRecord node)
{
    return float4(node.representative_normal, node.representative_plane_dist);
}

float3 AsspNodePlaneClosestPoint(AsspNodeRecord node)
{
    return node.representative_normal * node.representative_plane_dist;
}

AsspNodeRecord AsspLoadNode(uint lod_index, uint2 node_coord)
{
    if(!AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return AsspMakeInvalidNode();
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);

    AsspNodeRecord node;
    node.state = AsspBuffer[word_offset + 0];
    node.representative_texel = uint2(AsspBuffer[word_offset + 1], AsspBuffer[word_offset + 2]);
    node.front_depth = asfloat(AsspBuffer[word_offset + 3]);
    node.representative_normal = float3(
        asfloat(AsspBuffer[word_offset + 4]),
        asfloat(AsspBuffer[word_offset + 5]),
        asfloat(AsspBuffer[word_offset + 6]));
    node.representative_plane_dist = asfloat(AsspBuffer[word_offset + 7]);
    node.metric0 = asfloat(AsspBuffer[word_offset + 8]);
    node.metric1 = asfloat(AsspBuffer[word_offset + 9]);
    node.metric2 = asfloat(AsspBuffer[word_offset + 10]);
    node.split_score = asfloat(AsspBuffer[word_offset + 11]);
    return node;
}

void AsspStoreNode(uint lod_index, uint2 node_coord, AsspNodeRecord node)
{
    if(!AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return;
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);
    RWAsspBuffer[word_offset + 0] = node.state;
    RWAsspBuffer[word_offset + 1] = node.representative_texel.x;
    RWAsspBuffer[word_offset + 2] = node.representative_texel.y;
    RWAsspBuffer[word_offset + 3] = asuint(node.front_depth);
    RWAsspBuffer[word_offset + 4] = asuint(node.representative_normal.x);
    RWAsspBuffer[word_offset + 5] = asuint(node.representative_normal.y);
    RWAsspBuffer[word_offset + 6] = asuint(node.representative_normal.z);
    RWAsspBuffer[word_offset + 7] = asuint(node.representative_plane_dist);
    RWAsspBuffer[word_offset + 8] = asuint(node.metric0);
    RWAsspBuffer[word_offset + 9] = asuint(node.metric1);
    RWAsspBuffer[word_offset + 10] = asuint(node.metric2);
    RWAsspBuffer[word_offset + 11] = asuint(node.split_score);
}

AsspNodeRecord AsspLoadLod0NodeFromRepresentativeTexel(int2 representative_texel_pos)
{
    if(any(representative_texel_pos < 0))
    {
        return AsspMakeEmptyNode();
    }

    return AsspLoadNode(0u, AsspNodeCoordFromScreenTexel(representative_texel_pos, 0u));
}

void AsspResolveLeafNode(
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
    const AsspNodeRecord lod0_node = AsspLoadNode(0u, AsspNodeCoordFromScreenTexel(screen_texel_pos, 0u));
    selected_lod = 0;
    selected_node_origin = AsspNodeOriginFromCoord(AsspNodeCoordFromScreenTexel(screen_texel_pos, 0u), 0u);
    selected_node_size = k_assp_tile_size;
    representative_texel_pos = int2(lod0_node.representative_texel);
    representative_depth = lod0_node.front_depth;
    plane_error = lod0_node.metric1;
    split_score = lod0_node.split_score;
    representative_normal = AsspNodeIsSolid(lod0_node) ? lod0_node.representative_normal : float3(0.0, 0.0, 1.0);

    const float threshold = cb_srvs.assp_debug_split_threshold;
    for(int hierarchy_lod = cb_srvs.assp_lod_count - 1; hierarchy_lod >= 1; --hierarchy_lod)
    {
        const uint lod_index = (uint)hierarchy_lod;
        const uint2 hierarchy_texel_pos = AsspNodeCoordFromScreenTexel(screen_texel_pos, lod_index);
        const AsspNodeRecord node = AsspLoadNode(lod_index, hierarchy_texel_pos);
        if(!AsspNodeIsSolid(node))
        {
            continue;
        }

        if(node.split_score <= threshold)
        {
            selected_lod = hierarchy_lod;
            selected_node_origin = AsspNodeOriginFromCoord(hierarchy_texel_pos, lod_index);
            selected_node_size = AsspNodeSizeInPixels(lod_index);
            representative_texel_pos = int2(node.representative_texel);
            representative_depth = node.front_depth;
            plane_error = max(node.metric0, node.metric1);
            split_score = node.split_score;
            representative_normal = AsspNodeIsSolid(node) ? AsspLoadLod0NodeFromRepresentativeTexel(int2(node.representative_texel)).representative_normal : float3(0.0, 0.0, 1.0);
            return;
        }
    }
}

int2 AsspResolveRepresentativeTileId(int2 screen_texel_pos)
{
    int selected_lod;
    int2 selected_node_origin;
    int selected_node_size;
    int2 representative_texel_pos;
    float representative_depth;
    float plane_error;
    float split_score;
    float3 representative_normal;
    AsspResolveLeafNode(
        screen_texel_pos,
        selected_lod,
        selected_node_origin,
        selected_node_size,
        representative_texel_pos,
        representative_depth,
        plane_error,
        split_score,
        representative_normal);
    if(any(representative_texel_pos < 0))
    {
        return int2(-1, -1);
    }
    return representative_texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
}

#endif

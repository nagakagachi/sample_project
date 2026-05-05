#ifndef NGL_SHADER_SRVS_ASSP_BUFFER_UTIL_H
#define NGL_SHADER_SRVS_ASSP_BUFFER_UTIL_H

struct AsspLod0NodeRecord
{
    // state:
    //   invalid = 画面端のはみ出しなどで node 自体が存在しない
    //   empty   = node は存在するが深度サンプルがなく空領域
    //   solid   = representative を持つ有効 LOD0 node
    uint state;
    uint representative_texel_packed;
    float front_depth;
    float3 representative_normal;
    float representative_plane_dist;
    float plane_error;
    float split_score;
};

struct AsspHierarchyNodeRecord
{
    // state:
    //   invalid = 画面端のはみ出しなどで node 自体が存在しない
    //   empty   = child に active probe がない
    //   solid   = この node 自体が active leaf として採用された
    //   split   = child 側へ active leaf を残す branch node
    uint state;
    uint representative_texel_packed;
    uint subtree_active_probe_count;
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
    return AsspLodBaseWordOffsetFromCb(lod_index) + (node_coord.y * AsspLodWidthFromCb(lod_index) + node_coord.x) * AsspWordsPerNode(lod_index);
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

uint AsspPackRepresentativeTexel(uint2 representative_texel)
{
    return (representative_texel.x & 0xffffu) | ((representative_texel.y & 0xffffu) << 16u);
}

uint2 AsspUnpackRepresentativeTexel(uint representative_texel_packed)
{
    return uint2(representative_texel_packed & 0xffffu, representative_texel_packed >> 16u);
}

int2 AsspUnpackRepresentativeTexelInt2(uint representative_texel_packed)
{
    return int2(AsspUnpackRepresentativeTexel(representative_texel_packed));
}

uint AsspPackHierarchyStateAndCount(uint state, uint subtree_active_probe_count)
{
    return (subtree_active_probe_count << k_assp_hierarchy_state_bit_count) | (state & k_assp_hierarchy_state_mask);
}

uint AsspUnpackHierarchyState(uint packed_state_and_count)
{
    return packed_state_and_count & k_assp_hierarchy_state_mask;
}

uint AsspUnpackHierarchyActiveProbeCount(uint packed_state_and_count)
{
    return packed_state_and_count >> k_assp_hierarchy_state_bit_count;
}

AsspLod0NodeRecord AsspMakeInvalidLod0Node()
{
    AsspLod0NodeRecord node;
    node.state = k_assp_state_invalid;
    node.representative_texel_packed = 0xffffffffu;
    node.front_depth = -1.0;
    node.representative_normal = float3(0.0, 0.0, 1.0);
    node.representative_plane_dist = 0.0;
    node.plane_error = 0.0;
    node.split_score = 0.0;
    return node;
}

AsspLod0NodeRecord AsspMakeEmptyLod0Node()
{
    AsspLod0NodeRecord node = AsspMakeInvalidLod0Node();
    node.state = k_assp_state_empty;
    node.front_depth = 0.0;
    return node;
}

bool AsspLod0NodeIsValid(AsspLod0NodeRecord node)
{
    return node.state != k_assp_state_invalid;
}

bool AsspLod0NodeIsSolid(AsspLod0NodeRecord node)
{
    return node.state == k_assp_state_solid;
}

bool AsspLod0NodeIsEmpty(AsspLod0NodeRecord node)
{
    return node.state == k_assp_state_empty;
}

float4 AsspLod0NodePlane(AsspLod0NodeRecord node)
{
    return float4(node.representative_normal, node.representative_plane_dist);
}

float3 AsspLod0NodePlaneClosestPoint(AsspLod0NodeRecord node)
{
    return node.representative_normal * node.representative_plane_dist;
}

AsspHierarchyNodeRecord AsspMakeInvalidHierarchyNode()
{
    AsspHierarchyNodeRecord node;
    node.state = k_assp_state_invalid;
    node.representative_texel_packed = 0xffffffffu;
    node.subtree_active_probe_count = 0u;
    return node;
}

AsspHierarchyNodeRecord AsspMakeEmptyHierarchyNode()
{
    AsspHierarchyNodeRecord node = AsspMakeInvalidHierarchyNode();
    node.state = k_assp_state_empty;
    return node;
}

bool AsspHierarchyNodeIsValid(AsspHierarchyNodeRecord node)
{
    return node.state != k_assp_state_invalid;
}

bool AsspHierarchyNodeIsSolid(AsspHierarchyNodeRecord node)
{
    return node.state == k_assp_state_solid;
}

bool AsspHierarchyNodeIsSplit(AsspHierarchyNodeRecord node)
{
    return node.state == k_assp_state_split;
}

bool AsspHierarchyNodeIsEmpty(AsspHierarchyNodeRecord node)
{
    return node.state == k_assp_state_empty;
}

bool AsspHierarchyNodeHasRepresentative(AsspHierarchyNodeRecord node)
{
    return AsspHierarchyNodeIsSolid(node) || AsspHierarchyNodeIsSplit(node);
}

uint AsspHierarchyNodeActiveProbeCount(AsspHierarchyNodeRecord node)
{
    return AsspHierarchyNodeHasRepresentative(node) ? node.subtree_active_probe_count : 0u;
}

AsspHierarchyNodeRecord AsspMakeHierarchyNodeFromLod0(AsspLod0NodeRecord node)
{
    AsspHierarchyNodeRecord hierarchy_node = AsspMakeInvalidHierarchyNode();
    hierarchy_node.state = node.state;
    hierarchy_node.representative_texel_packed = node.representative_texel_packed;
    hierarchy_node.subtree_active_probe_count = AsspLod0NodeIsSolid(node) ? 1u : 0u;
    return hierarchy_node;
}

AsspLod0NodeRecord AsspLoadLod0Node(uint2 node_coord)
{
    const uint lod_index = 0u;
    if(!AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return AsspMakeInvalidLod0Node();
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);

    AsspLod0NodeRecord node = AsspMakeInvalidLod0Node();
    node.state = AsspBuffer[word_offset + 0];
    node.representative_texel_packed = AsspBuffer[word_offset + 1];
    node.front_depth = asfloat(AsspBuffer[word_offset + 2]);
    node.representative_normal = float3(
        asfloat(AsspBuffer[word_offset + 3]),
        asfloat(AsspBuffer[word_offset + 4]),
        asfloat(AsspBuffer[word_offset + 5]));
    node.representative_plane_dist = asfloat(AsspBuffer[word_offset + 6]);
    node.plane_error = asfloat(AsspBuffer[word_offset + 7]);
    node.split_score = asfloat(AsspBuffer[word_offset + 8]);
    return node;
}

void AsspStoreLod0Node(uint2 node_coord, AsspLod0NodeRecord node)
{
    const uint lod_index = 0u;
    if(!AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return;
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);
    RWAsspBuffer[word_offset + 0] = node.state;
    RWAsspBuffer[word_offset + 1] = node.representative_texel_packed;
    RWAsspBuffer[word_offset + 2] = asuint(node.front_depth);
    RWAsspBuffer[word_offset + 3] = asuint(node.representative_normal.x);
    RWAsspBuffer[word_offset + 4] = asuint(node.representative_normal.y);
    RWAsspBuffer[word_offset + 5] = asuint(node.representative_normal.z);
    RWAsspBuffer[word_offset + 6] = asuint(node.representative_plane_dist);
    RWAsspBuffer[word_offset + 7] = asuint(node.plane_error);
    RWAsspBuffer[word_offset + 8] = asuint(node.split_score);
}

AsspHierarchyNodeRecord AsspLoadHierarchyNode(uint lod_index, uint2 node_coord)
{
    if((0u == lod_index) || !AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return AsspMakeInvalidHierarchyNode();
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);

    AsspHierarchyNodeRecord node = AsspMakeInvalidHierarchyNode();
    const uint packed_state_and_count = AsspBuffer[word_offset + 0];
    node.state = AsspUnpackHierarchyState(packed_state_and_count);
    node.representative_texel_packed = AsspBuffer[word_offset + 1];
    node.subtree_active_probe_count = AsspUnpackHierarchyActiveProbeCount(packed_state_and_count);
    return node;
}

void AsspStoreHierarchyNode(uint lod_index, uint2 node_coord, AsspHierarchyNodeRecord node)
{
    if((0u == lod_index) || !AsspIsNodeCoordInRange(lod_index, node_coord))
    {
        return;
    }

    const uint word_offset = AsspNodeWordOffset(lod_index, node_coord);
    RWAsspBuffer[word_offset + 0] = AsspPackHierarchyStateAndCount(node.state, node.subtree_active_probe_count);
    RWAsspBuffer[word_offset + 1] = node.representative_texel_packed;
}

AsspHierarchyNodeRecord AsspLoadNodeForHierarchy(uint lod_index, uint2 node_coord)
{
    if(0u == lod_index)
    {
        return AsspMakeHierarchyNodeFromLod0(AsspLoadLod0Node(node_coord));
    }

    return AsspLoadHierarchyNode(lod_index, node_coord);
}

AsspLod0NodeRecord AsspLoadLod0NodeFromRepresentativeTexel(int2 representative_texel_pos)
{
    if(any(representative_texel_pos < 0))
    {
        return AsspMakeEmptyLod0Node();
    }

    return AsspLoadLod0Node(AsspNodeCoordFromScreenTexel(representative_texel_pos, 0u));
}

AsspLod0NodeRecord AsspLoadRepresentativeLod0Node(AsspHierarchyNodeRecord node)
{
    if(AsspHierarchyNodeHasRepresentative(node))
    {
        return AsspLoadLod0NodeFromRepresentativeTexel(AsspUnpackRepresentativeTexelInt2(node.representative_texel_packed));
    }

    return AsspMakeEmptyLod0Node();
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
    const AsspLod0NodeRecord lod0_node = AsspLoadLod0Node(AsspNodeCoordFromScreenTexel(screen_texel_pos, 0u));
    selected_lod = 0;
    selected_node_origin = AsspNodeOriginFromCoord(AsspNodeCoordFromScreenTexel(screen_texel_pos, 0u), 0u);
    selected_node_size = k_assp_tile_size;
    representative_texel_pos = AsspUnpackRepresentativeTexelInt2(lod0_node.representative_texel_packed);
    representative_depth = lod0_node.front_depth;
    plane_error = lod0_node.plane_error;
    split_score = lod0_node.split_score;
    representative_normal = AsspLod0NodeIsSolid(lod0_node) ? lod0_node.representative_normal : float3(0.0, 0.0, 1.0);

    // two-level 固定なので coarse LOD1 だけを見ればよい。
    const uint lod_index = 1u;
    const uint2 hierarchy_texel_pos = AsspNodeCoordFromScreenTexel(screen_texel_pos, lod_index);
    const AsspHierarchyNodeRecord node = AsspLoadHierarchyNode(lod_index, hierarchy_texel_pos);
    if(AsspHierarchyNodeIsSolid(node))
    {
        const AsspLod0NodeRecord representative_lod0_node = AsspLoadRepresentativeLod0Node(node);
        selected_lod = 1;
        selected_node_origin = AsspNodeOriginFromCoord(hierarchy_texel_pos, lod_index);
        selected_node_size = AsspNodeSizeInPixels(lod_index);
        representative_texel_pos = AsspUnpackRepresentativeTexelInt2(node.representative_texel_packed);
        representative_depth = representative_lod0_node.front_depth;
        plane_error = representative_lod0_node.plane_error;
        split_score = representative_lod0_node.split_score;
        representative_normal = representative_lod0_node.representative_normal;
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

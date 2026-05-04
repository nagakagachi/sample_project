#ifndef NGL_SHADER_SRVS_SAP_BUFFER_UTIL_H
#define NGL_SHADER_SRVS_SAP_BUFFER_UTIL_H

struct SapNodeRecord
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
    // record 幅を固定にして index/decode を単純に保つため、用途差分はコメント運用に寄せる。
    float metric0;
    float metric1;
    float metric2;
    float split_score;
};

uint SapScreenWidth()
{
    return (uint)max(cb_srvs.tex_main_view_depth_size.x, 1);
}

uint SapScreenHeight()
{
    return (uint)max(cb_srvs.tex_main_view_depth_size.y, 1);
}

uint SapLodWidthFromCb(uint lod_index)
{
    return SapLodWidth(SapScreenWidth(), lod_index);
}

uint SapLodHeightFromCb(uint lod_index)
{
    return SapLodHeight(SapScreenHeight(), lod_index);
}

uint SapLodBaseWordOffsetFromCb(uint lod_index)
{
    return SapLodBaseWordOffset(SapScreenWidth(), SapScreenHeight(), lod_index);
}

uint SapNodeWordOffset(uint lod_index, uint2 node_coord)
{
    // SapBuffer は typed R32_UINT buffer なので byte address ではなく word index で直接アクセスする。
    return SapLodBaseWordOffsetFromCb(lod_index) + (node_coord.y * SapLodWidthFromCb(lod_index) + node_coord.x) * (uint)cb_srvs.sap_words_per_node;
}

bool SapIsNodeCoordInRange(uint lod_index, uint2 node_coord)
{
    return (node_coord.x < SapLodWidthFromCb(lod_index)) && (node_coord.y < SapLodHeightFromCb(lod_index));
}

uint2 SapNodeCoordFromScreenTexel(int2 screen_texel_pos, uint lod_index)
{
    return uint2(screen_texel_pos) / SapNodeSizeInPixels(lod_index);
}

int2 SapNodeOriginFromCoord(uint2 node_coord, uint lod_index)
{
    return int2(node_coord * SapNodeSizeInPixels(lod_index));
}

SapNodeRecord SapMakeInvalidNode()
{
    SapNodeRecord node;
    node.state = k_sap_state_invalid;
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

SapNodeRecord SapMakeEmptyNode()
{
    SapNodeRecord node = SapMakeInvalidNode();
    node.state = k_sap_state_empty;
    node.front_depth = 0.0;
    return node;
}

bool SapNodeIsValid(SapNodeRecord node)
{
    return node.state != k_sap_state_invalid;
}

bool SapNodeIsSolid(SapNodeRecord node)
{
    return node.state == k_sap_state_solid;
}

bool SapNodeIsEmpty(SapNodeRecord node)
{
    return node.state == k_sap_state_empty;
}

float4 SapNodePlane(SapNodeRecord node)
{
    return float4(node.representative_normal, node.representative_plane_dist);
}

float3 SapNodePlaneClosestPoint(SapNodeRecord node)
{
    return node.representative_normal * node.representative_plane_dist;
}

SapNodeRecord SapLoadNode(uint lod_index, uint2 node_coord)
{
    if(!SapIsNodeCoordInRange(lod_index, node_coord))
    {
        return SapMakeInvalidNode();
    }

    const uint word_offset = SapNodeWordOffset(lod_index, node_coord);

    // 現段階は asuint/asfloat の素直な encode で切り替えコストを下げる。
    // 後で圧縮する場合も、この関数を差し替えれば利用側をほぼ保ったまま移行できる。
    SapNodeRecord node;
    node.state = SapBuffer[word_offset + 0];
    node.representative_texel = uint2(SapBuffer[word_offset + 1], SapBuffer[word_offset + 2]);
    node.front_depth = asfloat(SapBuffer[word_offset + 3]);
    node.representative_normal = float3(
        asfloat(SapBuffer[word_offset + 4]),
        asfloat(SapBuffer[word_offset + 5]),
        asfloat(SapBuffer[word_offset + 6]));
    node.representative_plane_dist = asfloat(SapBuffer[word_offset + 7]);
    node.metric0 = asfloat(SapBuffer[word_offset + 8]);
    node.metric1 = asfloat(SapBuffer[word_offset + 9]);
    node.metric2 = asfloat(SapBuffer[word_offset + 10]);
    node.split_score = asfloat(SapBuffer[word_offset + 11]);
    return node;
}

void SapStoreNode(uint lod_index, uint2 node_coord, SapNodeRecord node)
{
    if(!SapIsNodeCoordInRange(lod_index, node_coord))
    {
        return;
    }

    const uint word_offset = SapNodeWordOffset(lod_index, node_coord);
    // Word layout contract:
    //   0  state
    //   1  representative texel x
    //   2  representative texel y
    //   3  front depth
    //   4  representative normal x
    //   5  representative normal y
    //   6  representative normal z
    //   7  representative plane distance
    //   8  metric0
    //   9  metric1
    //   10 metric2
    //   11 split score
    // CPU/HLSL の両方でこの順序を共有し、debug も build pass も同じ decode を使う。
    RWSapBuffer[word_offset + 0] = node.state;
    RWSapBuffer[word_offset + 1] = node.representative_texel.x;
    RWSapBuffer[word_offset + 2] = node.representative_texel.y;
    RWSapBuffer[word_offset + 3] = asuint(node.front_depth);
    RWSapBuffer[word_offset + 4] = asuint(node.representative_normal.x);
    RWSapBuffer[word_offset + 5] = asuint(node.representative_normal.y);
    RWSapBuffer[word_offset + 6] = asuint(node.representative_normal.z);
    RWSapBuffer[word_offset + 7] = asuint(node.representative_plane_dist);
    RWSapBuffer[word_offset + 8] = asuint(node.metric0);
    RWSapBuffer[word_offset + 9] = asuint(node.metric1);
    RWSapBuffer[word_offset + 10] = asuint(node.metric2);
    RWSapBuffer[word_offset + 11] = asuint(node.split_score);
}

SapNodeRecord SapLoadLod0NodeFromRepresentativeTexel(int2 representative_texel_pos)
{
    if(any(representative_texel_pos < 0))
    {
        return SapMakeEmptyNode();
    }

    // 上位 LOD は representative texel だけを持つため、plane / normal / 詳細 metric が必要な場面では
    // 必ず LOD0 record へ戻って再参照する。
    return SapLoadNode(0u, SapNodeCoordFromScreenTexel(representative_texel_pos, 0u));
}

#endif

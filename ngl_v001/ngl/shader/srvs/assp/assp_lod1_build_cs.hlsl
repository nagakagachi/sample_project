#if 0

assp_lod1_build_cs.hlsl

ASSP LOD1 build.
LOD0 node から 2x2 child をまとめて representative texel indirection と metrics を作る。

#endif

#include "../srvs_util.hlsli"
#include "assp_buffer_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID)
{
    const uint2 out_size = uint2(AsspLodWidthFromCb(1u), AsspLodHeightFromCb(1u));
    if(any(dtid.xy >= out_size))
        return;

    AsspNodeRecord child_node[4];
    uint front_child_index = 0xffffffffu;
    float front_depth = 1e20;
    uint valid_child_count = 0;
    uint solid_child_count_for_score = 0;
    uint solid_child_count = 0;

    [unroll]
    for(int child_index = 0; child_index < 4; ++child_index)
    {
        const uint2 child_offset = uint2(child_index & 1, child_index >> 1);
        child_node[child_index] = AsspLoadNode(0u, dtid.xy * 2u + child_offset);
        valid_child_count += AsspNodeIsValid(child_node[child_index]) ? 1u : 0u;
        solid_child_count += AsspNodeIsSolid(child_node[child_index]) ? 1u : 0u;
        if(AsspNodeIsSolid(child_node[child_index]) && child_node[child_index].front_depth < front_depth)
        {
            front_depth = child_node[child_index].front_depth;
            front_child_index = child_index;
        }
    }

    if(0 == valid_child_count)
    {
        AsspStoreNode(1u, dtid.xy, AsspMakeInvalidNode());
        return;
    }

    if(0 == solid_child_count)
    {
        // すべて empty child なら親も empty node として保持し、split しない。
        AsspStoreNode(1u, dtid.xy, AsspMakeEmptyNode());
        return;
    }

    const AsspNodeRecord representative_lod0_node = AsspLoadLod0NodeFromRepresentativeTexel(int2(child_node[front_child_index].representative_texel));

    float max_residual = 0.0;
    float max_behind_gap = 0.0;
    [unroll]
    for(int child_index = 0; child_index < 4; ++child_index)
    {
        if(AsspNodeIsSolid(child_node[child_index]))
        {
            const AsspNodeRecord child_lod0_node = AsspLoadLod0NodeFromRepresentativeTexel(int2(child_node[child_index].representative_texel));
            const float3 child_point_vs = AsspNodePlaneClosestPoint(child_lod0_node);
            const float child_depth = child_node[child_index].front_depth;
            const float residual = abs(dot(representative_lod0_node.representative_normal, child_point_vs) - representative_lod0_node.representative_plane_dist);
            max_residual = max(max_residual, residual);
            // front-most child からどれだけ奥に面が離れているかを見る。
            // 同一平面に乗っていても、奥側に層が分かれていれば split を後押しする。
            max_behind_gap = max(max_behind_gap, max(0.0, child_depth - front_depth));
            solid_child_count_for_score += 1;
        }
    }

    // hierarchy 側は child node 単位の代表面比較なので、
    // LOD0 よりも residual をやや抑え、behind gap を明示的に評価する。
    const float depth_scale = max(front_depth, 1e-3);
    const float split_score = saturate(max_residual / depth_scale * 24.0 + max_behind_gap / depth_scale * 8.0 + (1.0 - float(solid_child_count_for_score) * 0.25) * 0.5);

    AsspNodeRecord out_node = AsspMakeInvalidNode();
    out_node.state = k_assp_state_solid;
    out_node.representative_texel = child_node[front_child_index].representative_texel;
    out_node.front_depth = front_depth;
    out_node.representative_normal = representative_lod0_node.representative_normal;
    out_node.representative_plane_dist = representative_lod0_node.representative_plane_dist;
    out_node.metric0 = max_residual;
    out_node.metric1 = max_behind_gap;
    out_node.metric2 = 0.0;
    out_node.split_score = split_score;
    AsspStoreNode(1u, dtid.xy, out_node);
}

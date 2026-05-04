#if 0

assp_hierarchy_build_cs.hlsl

ASSP LOD2+ build.
直下 LOD の 2x2 child node から build 時に merge/split を確定する。

#endif

#include "../srvs_util.hlsli"
#include "assp_buffer_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID)
{
    const uint output_lod = (uint)cb_srvs.assp_build_lod;
    const uint2 out_size = uint2(AsspLodWidthFromCb(output_lod), AsspLodHeightFromCb(output_lod));
    if(any(dtid.xy >= out_size))
        return;

    AsspHierarchyNodeRecord child_node[4];
    AsspLod0NodeRecord child_representative_lod0_node[4];
    uint front_child_index = 0xffffffffu;
    float front_depth = 1e20;
    uint valid_child_count = 0;
    uint active_child_count = 0;
    uint subtree_active_probe_count = 0;

    [unroll]
    for(int child_index = 0; child_index < 4; ++child_index)
    {
        const uint2 child_offset = uint2(child_index & 1, child_index >> 1);
        child_node[child_index] = AsspLoadNodeForHierarchy(output_lod - 1u, dtid.xy * 2u + child_offset);
        child_representative_lod0_node[child_index] = AsspMakeEmptyLod0Node();
        valid_child_count += AsspHierarchyNodeIsValid(child_node[child_index]) ? 1u : 0u;
        const uint child_active_probe_count = AsspHierarchyNodeActiveProbeCount(child_node[child_index]);
        subtree_active_probe_count += child_active_probe_count;
        if(0u != child_active_probe_count)
        {
            const AsspLod0NodeRecord child_lod0_node = AsspLoadRepresentativeLod0Node(child_node[child_index]);
            child_representative_lod0_node[child_index] = child_lod0_node;
            active_child_count += 1u;
            if(child_lod0_node.front_depth < front_depth)
            {
                front_depth = child_lod0_node.front_depth;
                front_child_index = child_index;
            }
        }
    }

    if(0 == valid_child_count)
    {
        AsspStoreHierarchyNode(output_lod, dtid.xy, AsspMakeInvalidHierarchyNode());
        return;
    }

    if(0u == subtree_active_probe_count)
    {
        // すべて empty child なら親も empty node として保持し、split しない。
        AsspStoreHierarchyNode(output_lod, dtid.xy, AsspMakeEmptyHierarchyNode());
        return;
    }

    const AsspLod0NodeRecord representative_lod0_node = child_representative_lod0_node[front_child_index];

    float max_residual = 0.0;
    float max_behind_gap = 0.0;
    [unroll]
    for(int child_index = 0; child_index < 4; ++child_index)
    {
        if(0u != AsspHierarchyNodeActiveProbeCount(child_node[child_index]))
        {
            const AsspLod0NodeRecord child_lod0_node = child_representative_lod0_node[child_index];
            const float3 child_point_vs = AsspLod0NodePlaneClosestPoint(child_lod0_node);
            const float child_depth = child_lod0_node.front_depth;
            const float residual = abs(dot(representative_lod0_node.representative_normal, child_point_vs) - representative_lod0_node.representative_plane_dist);
            max_residual = max(max_residual, residual);
            // front-most child からの奥方向距離。厚みや多層化の検出に使う。
            max_behind_gap = max(max_behind_gap, max(0.0, child_depth - front_depth));
        }
    }

    // LOD1 と同じ heuristic を higher LOD にも使い、LOD 間で split score の意味を揃える。
    const float depth_scale = max(front_depth, 1e-3);
    const float split_score = saturate(max_residual / depth_scale * 24.0 + max_behind_gap / depth_scale * 8.0 + (1.0 - float(active_child_count) * 0.25) * 0.5);
    const bool should_merge = (subtree_active_probe_count <= 1u) || (split_score <= cb_srvs.assp_debug_split_threshold);

    AsspHierarchyNodeRecord out_node = AsspMakeInvalidHierarchyNode();
    out_node.state = should_merge ? k_assp_state_solid : k_assp_state_split;
    out_node.representative_texel_packed = child_node[front_child_index].representative_texel_packed;
    out_node.subtree_active_probe_count = should_merge ? 1u : subtree_active_probe_count;
    AsspStoreHierarchyNode(output_lod, dtid.xy, out_node);
}

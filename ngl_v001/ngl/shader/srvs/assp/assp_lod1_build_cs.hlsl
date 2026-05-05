#if 0

assp_lod1_build_cs.hlsl

ASSP LOD1 build.
LOD0 node から 2x2 child をまとめて build 時に merge/split を確定する。

#endif

#include "assp_probe_common.hlsli"
#include "assp_buffer_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID)
{
    const uint2 out_size = uint2(AsspLodWidthFromCb(1u), AsspLodHeightFromCb(1u));
    if(any(dtid.xy >= out_size))
        return;

    AsspHierarchyNodeRecord child_node[4];
    AsspLod0NodeRecord child_representative_lod0_node[4];
    uint front_child_index = 0xffffffffu;
    float front_depth = 1e20;
    uint valid_child_count = 0;
    uint active_child_count = 0;
    uint subtree_active_probe_count = 0;
    float filtered_variance_sum = 0.0;
    float filtered_mean_sum = 0.0;
    float filtered_mean_sq_sum = 0.0;

    [unroll]
    for(int child_index = 0; child_index < 4; ++child_index)
    {
        const uint2 child_offset = uint2(child_index & 1, child_index >> 1);
        child_node[child_index] = AsspLoadNodeForHierarchy(0u, dtid.xy * 2u + child_offset);
        child_representative_lod0_node[child_index] = AsspMakeEmptyLod0Node();
        valid_child_count += AsspHierarchyNodeIsValid(child_node[child_index]) ? 1u : 0u;
        const uint child_active_probe_count = AsspHierarchyNodeActiveProbeCount(child_node[child_index]);
        subtree_active_probe_count += child_active_probe_count;
        if(0u != child_active_probe_count)
        {
            const AsspLod0NodeRecord child_lod0_node = AsspLoadRepresentativeLod0Node(child_node[child_index]);
            child_representative_lod0_node[child_index] = child_lod0_node;
            active_child_count += 1u;

            const int2 child_representative_tile_id = AsspUnpackRepresentativeTexelInt2(child_node[child_index].representative_texel_packed) / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            const float4 variance_signal = AdaptiveScreenSpaceProbeVarianceTex.Load(int3(child_representative_tile_id, 0));
            const float filtered_mean = max(variance_signal.x, 0.0);
            const float filtered_second_moment = max(variance_signal.y, 0.0);
            const float filtered_variance = max(filtered_second_moment - filtered_mean * filtered_mean, 0.0);
            filtered_variance_sum += filtered_variance;
            filtered_mean_sum += filtered_mean;
            filtered_mean_sq_sum += filtered_mean * filtered_mean;

            if(child_lod0_node.front_depth < front_depth)
            {
                front_depth = child_lod0_node.front_depth;
                front_child_index = child_index;
            }
        }
    }

    if(0 == valid_child_count)
    {
        AsspStoreHierarchyNode(1u, dtid.xy, AsspMakeInvalidHierarchyNode());
        return;
    }

    if(0u == subtree_active_probe_count)
    {
        // すべて empty child なら親も empty node として保持し、split しない。
        AsspStoreHierarchyNode(1u, dtid.xy, AsspMakeEmptyHierarchyNode());
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
            // front-most child からどれだけ奥に面が離れているかを見る。
            // 同一平面に乗っていても、奥側に層が分かれていれば split を後押しする。
            max_behind_gap = max(max_behind_gap, max(0.0, child_depth - front_depth));
        }
    }

    // hierarchy 側は child node 単位の代表面比較なので、
    // LOD0 よりも residual をやや抑え、behind gap を明示的に評価する。
    const float depth_scale = max(front_depth, 1e-3);
    const float plane_term = saturate(max_residual / depth_scale * 24.0 + max_behind_gap / depth_scale * 8.0 + (1.0 - float(active_child_count) * 0.25) * 0.5);
    const float active_child_count_f = max(float(active_child_count), 1.0);
    const float average_filtered_variance = filtered_variance_sum / active_child_count_f;
    const float average_filtered_mean = filtered_mean_sum / active_child_count_f;
    const float spatial_luminance_variance = max(filtered_mean_sq_sum / active_child_count_f - average_filtered_mean * average_filtered_mean, 0.0);
    const float combined_variance = average_filtered_variance + spatial_luminance_variance * 2.0;
    const float radiance_variance_term = combined_variance / (combined_variance + 0.02);
    const float weighted_plane_term = saturate(plane_term * cb_srvs.assp_lod_geometry_weight);
    const float weighted_radiance_variance_term = saturate(radiance_variance_term * cb_srvs.assp_lod_radiance_variance_weight);
    const float split_score = saturate(max(weighted_plane_term, weighted_radiance_variance_term));
    const bool should_merge = (subtree_active_probe_count <= 1u) || (split_score <= cb_srvs.assp_lod_split_score_threshold);

    AsspHierarchyNodeRecord out_node = AsspMakeInvalidHierarchyNode();
    out_node.state = should_merge ? k_assp_state_solid : k_assp_state_split;
    out_node.representative_texel_packed = child_node[front_child_index].representative_texel_packed;
    out_node.subtree_active_probe_count = should_merge ? 1u : subtree_active_probe_count;
    AsspStoreHierarchyNode(1u, dtid.xy, out_node);
}

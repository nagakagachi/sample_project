#if 0

sap_depth_analysis_cs.hlsl

ScreenAnalysisPass LOD0.
4x4 tile ごとの representative plane / representative texel / split metrics を
unified uint buffer の LOD0 範囲へそのまま encode する。

#endif

#include "../srvs_util.hlsli"
#include "sap_buffer_util.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D                     TexHardwareDepth;

bool SapTryLoadViewSpaceSample(
    int2 texel_pos,
    uint2 depth_size,
    out float linear_depth,
    out float3 sample_pos_vs)
{
    linear_depth = 0.0;
    sample_pos_vs = 0.0.xxx;

    if(any(texel_pos < 0) || any(texel_pos >= int2(depth_size)))
        return false;

    const float depth = TexHardwareDepth.Load(int3(texel_pos, 0)).r;
    if(!isValidDepth(depth))
        return false;

    const float view_z = calc_view_z_from_ndc_z(depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float2 uv = (float2(texel_pos) + 0.5) / float2(depth_size);
    sample_pos_vs = CalcViewSpacePosition(uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
    linear_depth = abs(view_z);
    return true;
}

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID)
{
    const uint2 lod0_size = uint2(SapLodWidthFromCb(0u), SapLodHeightFromCb(0u));
    if(any(dtid.xy >= lod0_size))
        return;

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);

    const int2 tile_origin = int2(dtid.xy * k_sap_tile_size);

    float3 sample_pos_vs[16];
    bool sample_valid[16];
    uint valid_count = 0;
    float min_depth = 1e20;
    float max_depth = 0.0;
    uint front_sample_index = 0xffffffffu;
    float front_depth = 1e20;

    [unroll]
    for(int sample_index = 0; sample_index < 16; ++sample_index)
    {
        sample_pos_vs[sample_index] = 0.0.xxx;
        sample_valid[sample_index] = false;
    }

    [unroll]
    for(int sy = 0; sy < k_sap_tile_size; ++sy)
    {
        [unroll]
        for(int sx = 0; sx < k_sap_tile_size; ++sx)
        {
            const int sample_index = sy * k_sap_tile_size + sx;
            const int2 sample_texel_pos = tile_origin + int2(sx, sy);

            float linear_depth = 0.0;
            float3 pos_vs = 0.0.xxx;
            if(SapTryLoadViewSpaceSample(sample_texel_pos, depth_size, linear_depth, pos_vs))
            {
                sample_valid[sample_index] = true;
                sample_pos_vs[sample_index] = pos_vs;
                min_depth = min(min_depth, linear_depth);
                max_depth = max(max_depth, linear_depth);
                if(linear_depth < front_depth)
                {
                    front_depth = linear_depth;
                    front_sample_index = sample_index;
                }
                valid_count += 1;
            }
        }
    }

    SapNodeRecord out_node = SapMakeEmptyNode();
    if(0 == valid_count || front_sample_index == 0xffffffffu)
    {
        // 完全に空のタイルは explicit empty node として保持し、
        // 上位 LOD でも coarse な empty 領域として継続利用する。
        SapStoreNode(0u, dtid.xy, out_node);
        return;
    }

    const int2 front_sample_pos_in_tile = int2(front_sample_index % k_sap_tile_size, front_sample_index / k_sap_tile_size);
    const int2 front_sample_texel_pos = tile_origin + front_sample_pos_in_tile;
    const float front_depth_ndc = TexHardwareDepth.Load(int3(front_sample_texel_pos, 0)).r;
    const float2 depth_size_inv = 1.0 / float2(depth_size);
    const float3 front_normal_vs = reconstruct_normal_vs_fine(
        TexHardwareDepth,
        front_sample_texel_pos,
        front_depth_ndc,
        depth_size_inv,
        cb_ngl_sceneview.cb_ndc_z_to_view_z_coef,
        cb_ngl_sceneview.cb_proj_mtx);
    const float front_normal_len_sq = dot(front_normal_vs, front_normal_vs);
    const float3 representative_normal_vs = (front_normal_len_sq > 1e-8) ? (front_normal_vs * rsqrt(front_normal_len_sq)) : float3(0.0, 0.0, 1.0);
    const float representative_plane_dist = dot(representative_normal_vs, sample_pos_vs[front_sample_index]);

    float residual_sum = 0.0;
    float max_residual = 0.0;
    [unroll]
    for(int sample_index = 0; sample_index < 16; ++sample_index)
    {
        if(sample_valid[sample_index])
        {
            const float residual = abs(dot(sample_pos_vs[sample_index], representative_normal_vs) - representative_plane_dist);
            residual_sum += residual;
            max_residual = max(max_residual, residual);
        }
    }

    // LOD0 は 4x4 固定タイルなので、valid ratio も 16 samples 基準で正規化する。
    const float valid_ratio = float(valid_count) / float(k_sap_tile_size * k_sap_tile_size);
    const float mean_residual = residual_sum / float(valid_count);
    const float depth_range = max_depth - min_depth;
    // 極端に浅い面でも過大評価になりすぎないよう、depth 正規化の下限を持たせる。
    const float depth_scale = max(front_depth, 1e-3);
    const float normalized_range = depth_range / depth_scale;
    const float normalized_max_residual = max_residual / depth_scale;
    const float normalized_mean_residual = mean_residual / depth_scale;
    // LOD0 の split score は conservative に寄せる:
    // - depth range は面の奥行き広がりを見るので中程度の重み
    // - max residual は局所的な破綻を拾いたいので最も強く効かせる
    // - mean residual は面全体の傾向を見るのでその次
    // - valid ratio penalty は欠損タイルを少しだけ押し上げる補助項
    const float split_score = saturate(normalized_range * 6.0 + normalized_max_residual * 32.0 + normalized_mean_residual * 16.0 + (1.0 - valid_ratio) * 0.5);

    out_node.state = k_sap_state_solid;
    out_node.representative_texel = uint2(front_sample_texel_pos);
    out_node.front_depth = front_depth;
    out_node.representative_normal = representative_normal_vs;
    out_node.representative_plane_dist = representative_plane_dist;
    out_node.metric0 = mean_residual;
    out_node.metric1 = max_residual;
    out_node.metric2 = depth_range;
    out_node.split_score = split_score;
    SapStoreNode(0u, dtid.xy, out_node);
}

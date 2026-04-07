/*
    depth_buffer_util.hlsli

    DepthBufferユーティリティ関数.
*/

#ifndef NGL_SHADER_DEPTH_BUFFER_UTIL_H
#define NGL_SHADER_DEPTH_BUFFER_UTIL_H
#include "ngl_shader_config.hlsli"
#include "math_util.hlsli"

// 上下左右の4サンプルから同一平面優先して法線を推定する.
// ごく細い段差付近で反転した法線がまれに発生する既知の不具合(平坦な床にある数ミリ~数センチの段差など).
// https://wickedengine.net/2019/09/improved-normal-reconstruction-from-depth/
float3 reconstruct_normal_vs_fine(Texture2D tex_hw_depth, int2 current_probe_texel_pos, float center_depth, float2 depth_size_inv, const float4 ndc_z_to_view_z_coef, float4x4 proj_mtx)
{
    const int2 center_pos = current_probe_texel_pos;
    const int2 right_pos = center_pos + int2(1, 0);
    const int2 left_pos = center_pos + int2(-1, 0);
    const int2 down_pos = center_pos + int2(0, 1);
    const int2 up_pos = center_pos + int2(0, -1);

    float horizontal_depths[2];
    float vertical_depths[2];
    horizontal_depths[0] = tex_hw_depth.Load(int3(right_pos, 0)).r;
    horizontal_depths[1] = tex_hw_depth.Load(int3(left_pos, 0)).r;
    vertical_depths[0] = tex_hw_depth.Load(int3(down_pos, 0)).r;
    vertical_depths[1] = tex_hw_depth.Load(int3(up_pos, 0)).r;

    // 左右と上下のペアで中心に近い方を選択して法線復元に使用するサンプルとする.
    const uint best_Z_horizontal = abs(horizontal_depths[0] - center_depth) < abs(horizontal_depths[1] - center_depth) ? 0 : 1;
    const uint best_Z_vertical = abs(vertical_depths[0] - center_depth) < abs(vertical_depths[1] - center_depth) ? 0 : 1;
    
    const float k_view_z_safe_limit = 100000.0;// 近傍サンプルは念のためクランプ. 極細のジオメトリと空の間の法線破綻を回避.
    // 選択されたサンプルの深度をビュー空間Zに変換.
    const float center_view_z = calc_view_z_from_ndc_z(center_depth, ndc_z_to_view_z_coef);
    const float horizontal_view_z = clamp(calc_view_z_from_ndc_z(horizontal_depths[best_Z_horizontal], ndc_z_to_view_z_coef), -k_view_z_safe_limit, k_view_z_safe_limit);
    const float vertical_view_z = clamp(calc_view_z_from_ndc_z(vertical_depths[best_Z_vertical], ndc_z_to_view_z_coef), -k_view_z_safe_limit, k_view_z_safe_limit);

    // 選択されたサンプルのビュー空間位置を計算.
    const float3 center_pos_vs = CalcViewSpacePosition((float2(center_pos) + 0.5) * depth_size_inv, center_view_z, proj_mtx);
    const float3 horizontal_pos_vs = CalcViewSpacePosition((float2(center_pos + (best_Z_horizontal == 0 ? int2(1, 0) : int2(-1, 0))) + 0.5) * depth_size_inv, horizontal_view_z, proj_mtx);
    const float3 vertical_pos_vs = CalcViewSpacePosition((float2(center_pos + (best_Z_vertical == 0 ? int2(0, 1) : int2(0, -1))) + 0.5) * depth_size_inv, vertical_view_z, proj_mtx);

    float3 p1 = 0, p2 = 0;
    if (best_Z_horizontal == 0 && best_Z_vertical == 1)
    {
        p1 = horizontal_pos_vs;
        p2 = vertical_pos_vs;
    }
    else if (best_Z_horizontal == 0 && best_Z_vertical == 0)
    {
        p1 = vertical_pos_vs;
        p2 = horizontal_pos_vs;
    }
    else if (best_Z_horizontal == 1 && best_Z_vertical == 1)
    {
        p1 = vertical_pos_vs;
        p2 = horizontal_pos_vs;
    }
    else if (best_Z_horizontal == 1 && best_Z_vertical == 0)
    {
        p1 = horizontal_pos_vs;
        p2 = vertical_pos_vs;
    }
    return normalize(cross(p2 - center_pos_vs, p1 - center_pos_vs));
}


#endif // NGL_SHADER_DEPTH_BUFFER_UTIL_H
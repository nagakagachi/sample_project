/*
    depth_buffer_util.hlsli

    DepthBufferユーティリティ関数.
*/

#ifndef NGL_SHADER_DEPTH_BUFFER_UTIL_H
#define NGL_SHADER_DEPTH_BUFFER_UTIL_H
#include "ngl_shader_config.hlsli"
#include "math_util.hlsli"

// 上下左右の4サンプルから同一平面優先して法線を推定する.
// https://wickedengine.net/2019/09/improved-normal-reconstruction-from-depth/
float3 reconstruct_normal_vs_fine(Texture2D tex_hw_depth, int2 current_probe_texel_pos, float center_depth, float2 depth_size_inv, const float4 ndc_z_to_view_z_coef, float4x4 proj_mtx)
{
    const int2 center_pos = current_probe_texel_pos;
    const int2 right_pos = center_pos + int2(1, 0);
    const int2 left_pos = center_pos + int2(-1, 0);
    const int2 down_pos = center_pos + int2(0, 1);
    const int2 up_pos = center_pos + int2(0, -1);

    // Load.
    float right_depth = tex_hw_depth.Load(int3(right_pos, 0)).r;
    float left_depth = tex_hw_depth.Load(int3(left_pos, 0)).r;
    float down_depth = tex_hw_depth.Load(int3(down_pos, 0)).r;
    float up_depth = tex_hw_depth.Load(int3(up_pos, 0)).r;
    
    const float center_view_z = calc_view_z_from_ndc_z(center_depth, ndc_z_to_view_z_coef);
    const float right_view_z = calc_view_z_from_ndc_z(right_depth, ndc_z_to_view_z_coef);
    const float left_view_z = calc_view_z_from_ndc_z(left_depth, ndc_z_to_view_z_coef);
    const float down_view_z = calc_view_z_from_ndc_z(down_depth, ndc_z_to_view_z_coef);
    const float up_view_z = calc_view_z_from_ndc_z(up_depth, ndc_z_to_view_z_coef);

    const float3 center_pos_vs = CalcViewSpacePosition((float2(center_pos) + 0.5) * depth_size_inv, center_view_z, proj_mtx);
    const float3 right_pos_vs = CalcViewSpacePosition((float2(right_pos) + 0.5) * depth_size_inv, right_view_z, proj_mtx);
    const float3 left_pos_vs = CalcViewSpacePosition((float2(left_pos) + 0.5) * depth_size_inv, left_view_z, proj_mtx);
    const float3 down_pos_vs = CalcViewSpacePosition((float2(down_pos) + 0.5) * depth_size_inv, down_view_z, proj_mtx);
    const float3 up_pos_vs = CalcViewSpacePosition((float2(up_pos) + 0.5) * depth_size_inv, up_view_z, proj_mtx);

	const uint best_Z_horizontal = abs(right_view_z - center_view_z) < abs(left_view_z - center_view_z) ? 1 : 2;
	const uint best_Z_vertical = abs(down_view_z - center_view_z) < abs(up_view_z - center_view_z) ? 3 : 4;

	float3 p1 = 0, p2 = 0;
    if (best_Z_horizontal == 1 && best_Z_vertical == 4)
	{
		p1 = right_pos_vs;
		p2 = up_pos_vs;
	}
	else if (best_Z_horizontal == 1 && best_Z_vertical == 3)
	{
		p1 = down_pos_vs;
		p2 = right_pos_vs;
	}
	else if (best_Z_horizontal == 2 && best_Z_vertical == 4)
	{
		p1 = up_pos_vs;
		p2 = left_pos_vs;
	}
	else if (best_Z_horizontal == 2 && best_Z_vertical == 3)
	{
		p1 = left_pos_vs;
		p2 = down_pos_vs;
	}

    return normalize(cross(p2 - center_pos_vs, p1 - center_pos_vs));
}


#endif // NGL_SHADER_DEPTH_BUFFER_UTIL_H
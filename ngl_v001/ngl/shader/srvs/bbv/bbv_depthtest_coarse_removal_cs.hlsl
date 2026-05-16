#if 0
bbv_depthtest_coarse_removal_cs.hlsl

DepthTest ベース更新向けの Removal。
Frustum 候補 Brick に対して bitcell 単位の深度テストを行い、
手前側の fine voxel だけを除去する。
#endif

#include "../srvs_util.hlsli"
// calc_view_z_from_ndc_z 定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;
Texture2D TexHardwareDepth;

[numthreads(64, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint brick_count = bbv_brick_count();
    if(dtid.x >= brick_count)
    {
        return;
    }

    const uint packed_index = FrustumBrickList[dtid.x + 1];
    if(0 == packed_index)
    {
        return;
    }
    const uint voxel_index = packed_index - 1;
    const int3 voxel_coord_toroidal = index_to_voxel_coord(voxel_index, cb_srvs.bbv.grid_resolution);
    const int3 voxel_coord_linear = voxel_coord_toroidal_mapping(
        voxel_coord_toroidal,
        cb_srvs.bbv.grid_resolution - cb_srvs.bbv.grid_toroidal_offset,
        cb_srvs.bbv.grid_resolution);

    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
    [unroll]
    for(uint u32_offset = 0; u32_offset < k_bbv_per_voxel_bitmask_u32_count; ++u32_offset)
    {
        uint bit_block = RWBitmaskBrickVoxel[bbv_addr + u32_offset];
        if(0 == bit_block)
        {
            continue;
        }

        uint remain_mask = bit_block;
        [unroll]
        for(uint bit_in_u32 = 0; bit_in_u32 < 32; ++bit_in_u32)
        {
            const uint bit_value = (1u << bit_in_u32);
            if(0 == (remain_mask & bit_value))
            {
                continue;
            }

            const uint bit_index = u32_offset * 32 + bit_in_u32;
            const uint3 bitcell_pos = calc_bbv_bitcell_pos_from_bit_index(bit_index);
            const float3 bitcell_center_ws =
                ((float3(voxel_coord_linear) + (float3(bitcell_pos) + 0.5) * k_bbv_per_voxel_resolution_inv) * cb_srvs.bbv.cell_size)
                + cb_srvs.bbv.grid_min_pos;
            const float3 bitcell_center_vs = mul(cb_injection_src_view_info.cb_view_mtx, float4(bitcell_center_ws, 1.0));
            const float4 bitcell_center_cs = mul(cb_injection_src_view_info.cb_proj_mtx, float4(bitcell_center_vs, 1.0));
            if(abs(bitcell_center_cs.w) <= 1e-6)
            {
                continue;
            }

            const float3 ndc = bitcell_center_cs.xyz / bitcell_center_cs.w;
            const float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
            if(any(uv < 0.0) || any(uv > 1.0))
            {
                continue;
            }

            const int2 depth_size = cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw;
            const int2 screen_pos = clamp(int2(uv * float2(depth_size)), int2(0, 0), depth_size - 1);
            const int2 atlas_pos = screen_pos + cb_injection_src_view_info.cb_view_depth_buffer_offset_size.xy;
            const float surface_depth = TexHardwareDepth.Load(int3(atlas_pos, 0)).r;
            if(!isValidDepth(surface_depth))
            {
                // 背景(最遠方)など有効深度がない画素は、この視線上の既存 voxel を除去する。
                // これにより空領域へ残留した occupancy をクリアできる。
                remain_mask &= ~bit_value;
                continue;
            }

            const float surface_view_z = abs(calc_view_z_from_ndc_z(surface_depth, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef));
            const float bitcell_view_z = abs(bitcell_center_vs.z);
            if(bitcell_view_z < surface_view_z)
            {
                remain_mask &= ~bit_value;
            }
        }

        RWBitmaskBrickVoxel[bbv_addr + u32_offset] = remain_mask;
    }
}

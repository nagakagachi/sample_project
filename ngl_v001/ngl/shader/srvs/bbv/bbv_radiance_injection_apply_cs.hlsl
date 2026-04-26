#if 0

bbv_radiance_injection_apply_cs.hlsl

MainView の HDR radiance + depth から BBV Brick ごとの radiance accumulation へ atomic 加算する.

#endif

#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;

Texture2D TexHardwareDepth;
Texture2D<float4> TexInputRadiance;

[numthreads(k_bbv_radiance_injection_tile_width, k_bbv_radiance_injection_tile_width, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    const int2 src_resolution = cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw;
    const int2 group_grid_resolution = bbv_radiance_injection_group_grid_resolution(src_resolution);
    // dispatch は全 tile ではなく 2x2 group 数まで圧縮している。
    if(any(int2(gid.xy) >= group_grid_resolution))
    {
        return;
    }

    int2 tile_coord = 0;
    // group id から、このフレームで処理すべき group 内 1 tile を復元する。
    if(!bbv_radiance_injection_group_coord_to_tile_coord(gid.xy, tile_coord, src_resolution))
    {
        return;
    }

    const int2 src_texel_in_view = tile_coord * k_bbv_radiance_injection_tile_width + int2(gtid.xy);
    if(any(src_texel_in_view >= src_resolution))
    {
        return;
    }

    const int2 src_texel = src_texel_in_view + cb_injection_src_view_info.cb_view_depth_buffer_offset_size.xy;
    const float depth = TexHardwareDepth.Load(int3(src_texel, 0)).r;
    if(!isValidDepth(depth))
    {
        return;
    }

    const float view_z = calc_view_z_from_ndc_z(depth, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef);
    const float2 screen_uv = (float2(src_texel_in_view) + 0.5.xx) / float2(src_resolution);
    const float3 pos_ws = mul(cb_injection_src_view_info.cb_view_inv_mtx, float4(CalcViewSpacePosition(screen_uv, view_z, cb_injection_src_view_info.cb_proj_mtx), 1.0));

    const float3 voxel_coordf = (pos_ws - cb_srvs.bbv.grid_min_pos) * cb_srvs.bbv.cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(any(voxel_coord < 0) || any(voxel_coord >= cb_srvs.bbv.grid_resolution))
    {
        return;
    }

    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_srvs.bbv.grid_resolution);

    const float3 input_radiance = max(TexInputRadiance.Load(int3(src_texel, 0)).rgb, 0.0.xxx);
    const float3 clamped_radiance = min(input_radiance, k_bbv_radiance_input_clamp.xxx);
    const uint3 fixed_point_radiance = (uint3)(clamped_radiance * k_bbv_radiance_fixed_point_scale + 0.5.xxx);

    InterlockedAdd(RWBbvRadianceAccumBuffer[bbv_radiance_accum_r_addr(voxel_index)], fixed_point_radiance.x);
    InterlockedAdd(RWBbvRadianceAccumBuffer[bbv_radiance_accum_g_addr(voxel_index)], fixed_point_radiance.y);
    InterlockedAdd(RWBbvRadianceAccumBuffer[bbv_radiance_accum_b_addr(voxel_index)], fixed_point_radiance.z);
    InterlockedAdd(RWBbvRadianceAccumBuffer[bbv_radiance_accum_count_addr(voxel_index)], 1);
}

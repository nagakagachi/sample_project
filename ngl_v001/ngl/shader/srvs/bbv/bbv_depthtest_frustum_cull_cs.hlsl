#if 0
bbv_depthtest_frustum_cull_cs.hlsl

DepthTest ベース更新向けの Frustum Cull。
候補 Brick を RWFrustumBrickList に収集する。
#endif

#include "../srvs_util.hlsli"

ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;

[numthreads(64, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint brick_count = bbv_brick_count();
    if(dtid.x >= brick_count)
    {
        return;
    }

    const int3 voxel_coord_toroidal = index_to_voxel_coord(dtid.x, cb_srvs.bbv.grid_resolution);
    const int3 voxel_coord_linear = voxel_coord_toroidal_mapping(
        voxel_coord_toroidal,
        cb_srvs.bbv.grid_resolution - cb_srvs.bbv.grid_toroidal_offset,
        cb_srvs.bbv.grid_resolution);

    const float3 brick_center_ws = (float3(voxel_coord_linear) + 0.5) * cb_srvs.bbv.cell_size + cb_srvs.bbv.grid_min_pos;
    const float3 brick_center_vs = mul(cb_injection_src_view_info.cb_view_mtx, float4(brick_center_ws, 1.0));
    const float4 brick_center_cs = mul(cb_injection_src_view_info.cb_proj_mtx, float4(brick_center_vs, 1.0));
    if(abs(brick_center_cs.w) <= 1e-6)
    {
        return;
    }

    const float3 ndc = brick_center_cs.xyz / brick_center_cs.w;
    const bool inside_frustum =
        (abs(ndc.x) <= 1.0) &&
        (abs(ndc.y) <= 1.0);
    if(!inside_frustum)
    {
        RWFrustumBrickList[dtid.x + 1] = 0;
        return;
    }
    RWFrustumBrickList[dtid.x + 1] = dtid.x + 1;
}

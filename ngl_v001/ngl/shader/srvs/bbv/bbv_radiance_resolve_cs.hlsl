#if 0

bbv_radiance_resolve_cs.hlsl

BBV Brick radiance accumulation を平均化し BbvOptionalData へ書き戻す.

#endif

#include "../srvs_util.hlsli"

[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // dispatch は Brick 全数ではなく 2x2x2 group 数まで圧縮している。
    if(dtid.x >= bbv_radiance_resolve_dispatch_count())
    {
        return;
    }

    int3 voxel_coord = 0;
    // dispatch index から、このフレームで処理すべき group 内 1 Brick を復元する。
    if(!bbv_radiance_resolve_dispatch_index_to_voxel_coord(dtid.x, voxel_coord))
    {
        return;
    }

    const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_srvs.bbv.grid_resolution);

    BbvOptionalData voxel_optional_data = RWBitmaskBrickVoxelOptionData[voxel_index];
    const uint sample_count = RWBbvRadianceAccumBuffer[bbv_radiance_accum_count_addr(voxel_index)];
    if(0 != sample_count)
    {
        const float inv_denominator = 1.0 / (k_bbv_radiance_fixed_point_scale * float(sample_count));
        const float3 current_resolved_radiance = float3(
            RWBbvRadianceAccumBuffer[bbv_radiance_accum_r_addr(voxel_index)],
            RWBbvRadianceAccumBuffer[bbv_radiance_accum_g_addr(voxel_index)],
            RWBbvRadianceAccumBuffer[bbv_radiance_accum_b_addr(voxel_index)]) * inv_denominator;
        voxel_optional_data.resolved_radiance = lerp(voxel_optional_data.resolved_radiance, current_resolved_radiance, 0.5);
    }
    voxel_optional_data.resolved_sample_count = sample_count;
    RWBitmaskBrickVoxelOptionData[voxel_index] = voxel_optional_data;

    RWBbvRadianceAccumBuffer[bbv_radiance_accum_r_addr(voxel_index)] = 0;
    RWBbvRadianceAccumBuffer[bbv_radiance_accum_g_addr(voxel_index)] = 0;
    RWBbvRadianceAccumBuffer[bbv_radiance_accum_b_addr(voxel_index)] = 0;
    RWBbvRadianceAccumBuffer[bbv_radiance_accum_count_addr(voxel_index)] = 0;
}

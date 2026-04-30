#if 0

fsp_probe_sh_update_cs.hlsl

 FrustumSurfaceProbe の OctMap atlas から SkyVisibility + Radiance の packed L1 SH atlas を毎フレーム再構築する.
 atlas coeff order:
   0 = Y00
   1 = Y1_{-1}(y)
   2 = Y1_0(z)
   3 = Y1_{+1}(x)
 packed RGBA:
   R = SkyVisibility coeff
   G = Radiance R coeff
   B = Radiance G coeff
   A = Radiance B coeff

#endif

#include "../srvs_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint active_probe_count = FspActiveProbeListCurr[0];
    if(dtid.x >= active_probe_count)
    {
        return;
    }

    const uint probe_index = FspActiveProbeListCurr[dtid.x + 1];
    if(probe_index >= cb_srvs.fsp_probe_pool_size)
    {
        return;
    }

    uint2 packed_sh_tex_size;
    RWFspProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const int2 logical_sh_tex_size = int2(packed_sh_tex_size >> 1);
    const int2 probe_tile_id = int2(FspProbeAtlasMapPos(probe_index));

    const FspProbePoolData probe_pool_data = FspProbePoolBuffer[probe_index];
    if(0 == (probe_pool_data.flags & k_fsp_probe_flag_allocated))
    {
        [unroll]
        for(uint coeff_index = 0; coeff_index < 4; ++coeff_index)
        {
            RWFspProbePackedSHTex[FspPackedShAtlasTexelCoord(probe_tile_id, coeff_index, logical_sh_tex_size)] = 0.0.xxxx;
        }
        return;
    }

    float4 packed_sh_coeff0 = 0.0.xxxx;
    float4 packed_sh_coeff1 = 0.0.xxxx;
    float4 packed_sh_coeff2 = 0.0.xxxx;
    float4 packed_sh_coeff3 = 0.0.xxxx;

    [unroll]
    for(int oy = 0; oy < k_fsp_probe_octmap_width; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < k_fsp_probe_octmap_width; ++ox)
        {
            const uint2 atlas_texel_pos = FspProbeAtlasTexelCoord(probe_index, uint2(ox, oy));
            const float4 fsp_probe_value = FspProbeAtlasTex.Load(int3(atlas_texel_pos, 0));
            const float4 packed_sample = float4(fsp_probe_value.a, fsp_probe_value.rgb);

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5.xx) / float(k_fsp_probe_octmap_width);
            const float3 dir_ws = OctDecode(oct_uv);
            const float4 sh_basis = EvaluateL1ShBasis(dir_ws);

            packed_sh_coeff0 += packed_sample * sh_basis.x;
            packed_sh_coeff1 += packed_sample * sh_basis.y;
            packed_sh_coeff2 += packed_sample * sh_basis.z;
            packed_sh_coeff3 += packed_sample * sh_basis.w;
        }
    }

    const float texel_solid_angle = (4.0 * 3.14159265359) / float(k_fsp_probe_octmap_width * k_fsp_probe_octmap_width);
    RWFspProbePackedSHTex[FspPackedShAtlasTexelCoord(probe_tile_id, 0, logical_sh_tex_size)] = packed_sh_coeff0 * texel_solid_angle;
    RWFspProbePackedSHTex[FspPackedShAtlasTexelCoord(probe_tile_id, 1, logical_sh_tex_size)] = packed_sh_coeff1 * texel_solid_angle;
    RWFspProbePackedSHTex[FspPackedShAtlasTexelCoord(probe_tile_id, 2, logical_sh_tex_size)] = packed_sh_coeff2 * texel_solid_angle;
    RWFspProbePackedSHTex[FspPackedShAtlasTexelCoord(probe_tile_id, 3, logical_sh_tex_size)] = packed_sh_coeff3 * texel_solid_angle;
}

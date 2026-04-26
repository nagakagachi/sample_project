#if 0

ss_probe_radiance_sh_update_cs.hlsl

ScreenSpaceProbe の最新フレーム OctMap から Radiance の L1 SH を毎フレーム再構築する.
RGBA = Y00, Y1_{-1}(y), Y1_0(z), Y1_{+1}(x) (standard SH L1 order)

#endif

#include "../srvs_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 sh_tex_size;
    RWScreenSpaceProbeRadianceSHTexR.GetDimensions(sh_tex_size.x, sh_tex_size.y);
    if(any(dtid.xy >= sh_tex_size))
    {
        return;
    }

    const int2 probe_tile_id = int2(dtid.xy);
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
    if(!isValidDepth(ss_probe_tile_info.x))
    {
        RWScreenSpaceProbeRadianceSHTexR[probe_tile_id] = float4(0.0, 0.0, 0.0, 0.0);
        RWScreenSpaceProbeRadianceSHTexG[probe_tile_id] = float4(0.0, 0.0, 0.0, 0.0);
        RWScreenSpaceProbeRadianceSHTexB[probe_tile_id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float3 probe_normal_ws = OctDecode(ss_probe_tile_info.zw);
    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

    float4 sh_coeff_r = float4(0.0, 0.0, 0.0, 0.0);
    float4 sh_coeff_g = float4(0.0, 0.0, 0.0, 0.0);
    float4 sh_coeff_b = float4(0.0, 0.0, 0.0, 0.0);

    // OctahedronMap to SH.
    [unroll]
    for(int oy = 0; oy < SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++ox)
        {
            const int2 atlas_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_OCT_RESOLUTION + int2(ox, oy);
            const float3 radiance = ScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0)).rgb;

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5) * SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);
            const float4 sh_basis = EvaluateL1ShBasis(dir_ws);

            sh_coeff_r += radiance.r * sh_basis;
            sh_coeff_g += radiance.g * sh_basis;
            sh_coeff_b += radiance.b * sh_basis;
        }
    }

#if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
    const float texel_solid_angle = (2.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#else
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#endif
    RWScreenSpaceProbeRadianceSHTexR[probe_tile_id] = sh_coeff_r * texel_solid_angle;
    RWScreenSpaceProbeRadianceSHTexG[probe_tile_id] = sh_coeff_g * texel_solid_angle;
    RWScreenSpaceProbeRadianceSHTexB[probe_tile_id] = sh_coeff_b * texel_solid_angle;
}

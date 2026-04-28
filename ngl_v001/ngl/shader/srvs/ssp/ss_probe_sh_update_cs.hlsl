#if 0

ss_probe_sh_update_cs.hlsl

 ScreenSpaceProbe の最新フレーム OctMap から SkyVisibility + Radiance の packed L1 SH atlas を毎フレーム再構築する.
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

 packed SH には plain sky visibility / plain radiance を保存する。
 cosine convolution は lighting 評価時に適用する。

#endif

#include "../srvs_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 packed_sh_tex_size;
    RWScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const int2 logical_sh_tex_size = int2(packed_sh_tex_size >> 1);
    if(any(dtid.xy >= logical_sh_tex_size))
    {
        return;
    }

    const int2 probe_tile_id = int2(dtid.xy);
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
    if(!isValidDepth(ss_probe_tile_info.x))
    {
        [unroll]
        for(uint coeff_index = 0; coeff_index < 4; ++coeff_index)
        {
            RWScreenSpaceProbePackedSHTex[SspPackedShAtlasTexelCoord(probe_tile_id, coeff_index, logical_sh_tex_size)] = float4(0.0, 0.0, 0.0, 0.0);
        }
        return;
    }

    const float3 probe_normal_ws = OctDecode(ss_probe_tile_info.zw);
    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

    float4 packed_sh_coeff0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff2 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff3 = float4(0.0, 0.0, 0.0, 0.0);

    // OctahedronMap to SH. Store plain directional visibility / radiance coefficients.
    [unroll]
    for(int oy = 0; oy < SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++ox)
        {
            const int2 atlas_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_OCT_RESOLUTION + int2(ox, oy);
            const float4 ss_probe_value = ScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0));
            const float4 packed_sample = float4(ss_probe_value.a, ss_probe_value.rgb);

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5) * SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);

            // SideCacheありの場合は逆向きを棄却しないこちらの方が品質向上する.
            const float4 sh_basis = EvaluateL1ShBasis(dir_ws);
            packed_sh_coeff0 += packed_sample * sh_basis.x;
            packed_sh_coeff1 += packed_sample * sh_basis.y;
            packed_sh_coeff2 += packed_sample * sh_basis.z;
            packed_sh_coeff3 += packed_sample * sh_basis.w;
        }
    }

#if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
    const float texel_solid_angle = (2.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#else
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#endif
    RWScreenSpaceProbePackedSHTex[SspPackedShAtlasTexelCoord(probe_tile_id, 0, logical_sh_tex_size)] = packed_sh_coeff0 * texel_solid_angle;
    RWScreenSpaceProbePackedSHTex[SspPackedShAtlasTexelCoord(probe_tile_id, 1, logical_sh_tex_size)] = packed_sh_coeff1 * texel_solid_angle;
    RWScreenSpaceProbePackedSHTex[SspPackedShAtlasTexelCoord(probe_tile_id, 2, logical_sh_tex_size)] = packed_sh_coeff2 * texel_solid_angle;
    RWScreenSpaceProbePackedSHTex[SspPackedShAtlasTexelCoord(probe_tile_id, 3, logical_sh_tex_size)] = packed_sh_coeff3 * texel_solid_angle;
}

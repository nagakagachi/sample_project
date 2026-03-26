#if 0

ss_probe_sh_update_cs.hlsl

ScreenSpaceProbeの最新フレームOctMapからL1 SHを毎フレーム再構築する.
rgba = l00, l1x, l1y, l1z

#endif

#include "srvs_util.hlsli"

[numthreads(8, 8, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 sh_tex_size;
    RWScreenSpaceProbeSHTex.GetDimensions(sh_tex_size.x, sh_tex_size.y);
    if(any(dtid.xy >= sh_tex_size))
    {
        return;
    }

    const int2 probe_tile_id = int2(dtid.xy);
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
    if(!isValidDepth(ss_probe_tile_info.x))
    {
        RWScreenSpaceProbeSHTex[probe_tile_id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float3 probe_normal_ws = OctDecode(ss_probe_tile_info.zw);
    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

    float4 sh_coeff = float4(0.0, 0.0, 0.0, 0.0);

    // OctahedronMap to SH.
    [unroll]
    for(int oy = 0; oy < SCREEN_SPACE_PROBE_TILE_SIZE; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < SCREEN_SPACE_PROBE_TILE_SIZE; ++ox)
        {
            const int2 atlas_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE + int2(ox, oy);
            const float visibility = ScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0)).r;

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);
            
            // SideCacheありの場合は逆向きを棄却しないこちらの方が品質向上する.
            sh_coeff += visibility * EvaluateL1ShBasis(dir_ws);
        }
    }

#if NGL_SSP_HEMI_OCTMAP
    const float texel_solid_angle = (2.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_TILE_TEXEL_COUNT);
#else
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_TILE_TEXEL_COUNT);
#endif
    RWScreenSpaceProbeSHTex[probe_tile_id] = sh_coeff * texel_solid_angle;
}
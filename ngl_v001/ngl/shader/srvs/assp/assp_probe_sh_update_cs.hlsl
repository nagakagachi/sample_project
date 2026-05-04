#if 0

assp_probe_sh_update_cs.hlsl

AdaptiveScreenSpaceProbe の最新フレーム 4x4 OctMap から packed L1 SH atlas を再構築する。

#endif

#include "assp_probe_common.hlsli"
#include "assp_buffer_util.hlsli"

void AsspStoreZeroPackedSh(int2 probe_tile_id, int2 logical_sh_tex_size)
{
    [unroll]
    for(uint coeff_index = 0; coeff_index < 4; ++coeff_index)
    {
        RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, coeff_index, logical_sh_tex_size)] = float4(0.0, 0.0, 0.0, 0.0);
    }
}

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    uint2 packed_sh_tex_size;
    RWAdaptiveScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const int2 logical_sh_tex_size = int2(packed_sh_tex_size >> 1);
    if(any(dtid.xy >= logical_sh_tex_size))
    {
        return;
    }

    const int2 probe_tile_id = int2(dtid.xy);
    const int2 probe_tile_pixel_start = probe_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const int2 representative_tile_id = AsspResolveRepresentativeTileId(probe_tile_pixel_start);
    if(any(representative_tile_id < 0) || any(representative_tile_id != probe_tile_id))
    {
        AsspStoreZeroPackedSh(probe_tile_id, logical_sh_tex_size);
        return;
    }

    const float4 probe_tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
    if(!isValidDepth(probe_tile_info.x))
    {
        AsspStoreZeroPackedSh(probe_tile_id, logical_sh_tex_size);
        return;
    }

    const float3 probe_normal_ws = OctDecode(probe_tile_info.zw);
    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

    float4 packed_sh_coeff0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff2 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff3 = float4(0.0, 0.0, 0.0, 0.0);

    [unroll]
    for(int oy = 0; oy < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION; ++ox)
        {
            const int2 atlas_texel_pos = probe_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + int2(ox, oy);
            const float4 probe_value = AdaptiveScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0));
            const float4 packed_sample = float4(probe_value.a, probe_value.rgb);

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);
            const float4 sh_basis = EvaluateL1ShBasis(dir_ws);
            packed_sh_coeff0 += packed_sample * sh_basis.x;
            packed_sh_coeff1 += packed_sample * sh_basis.y;
            packed_sh_coeff2 += packed_sample * sh_basis.z;
            packed_sh_coeff3 += packed_sample * sh_basis.w;
        }
    }

#if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
    const float texel_solid_angle = (2.0 * 3.14159265359) / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#else
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#endif
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 0, logical_sh_tex_size)] = packed_sh_coeff0 * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 1, logical_sh_tex_size)] = packed_sh_coeff1 * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 2, logical_sh_tex_size)] = packed_sh_coeff2 * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 3, logical_sh_tex_size)] = packed_sh_coeff3 * texel_solid_angle;
}

#if 0

assp_probe_sh_update_cs.hlsl

AdaptiveScreenSpaceProbe の最新フレーム 4x4 OctMap から packed L1 SH atlas を再構築する。

#endif

#include "assp_probe_common.hlsli"

void AsspStoreZeroPackedSh(int2 probe_tile_id, int2 logical_sh_tex_size)
{
    [unroll]
    for(uint coeff_index = 0; coeff_index < 4; ++coeff_index)
    {
        RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, coeff_index, logical_sh_tex_size)] = float4(0.0, 0.0, 0.0, 0.0);
    }
}

groupshared float4 gs_packed_sh_coeff0[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float4 gs_packed_sh_coeff1[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float4 gs_packed_sh_coeff2[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];
groupshared float4 gs_packed_sh_coeff3[ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE];

[numthreads(ADAPTIVE_SCREEN_SPACE_PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint representative_probe_count = AsspRepresentativeProbeList[0];
    const uint probe_group_local_index = gindex / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint probe_list_index = gid.x * ADAPTIVE_SCREEN_SPACE_PROBE_PROBE_PER_GROUP + probe_group_local_index;
    const uint local_probe_texel_index = gindex % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    const uint local_probe_lane = local_probe_texel_index;
    const bool is_probe_list_index_valid = probe_list_index < representative_probe_count;

    uint2 packed_sh_tex_size;
    RWAdaptiveScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const int2 logical_sh_tex_size = int2(packed_sh_tex_size >> 1);
    const int2 probe_tile_id = is_probe_list_index_valid ? AsspUnpackProbeTileId(AsspRepresentativeProbeList[probe_list_index + 1u]) : int2(-1, -1);
    const bool is_probe_tile_in_range = is_probe_list_index_valid && all(probe_tile_id >= 0) && all(probe_tile_id < logical_sh_tex_size);

    float4 packed_sh_coeff0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff2 = float4(0.0, 0.0, 0.0, 0.0);
    float4 packed_sh_coeff3 = float4(0.0, 0.0, 0.0, 0.0);
    bool is_probe_tile_valid = false;

    if(is_probe_tile_in_range)
    {
        const float4 probe_tile_info = AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(probe_tile_id, 0));
        is_probe_tile_valid = isValidDepth(probe_tile_info.x);

        if(is_probe_tile_valid)
        {
        const float3 probe_normal_ws = OctDecode(probe_tile_info.zw);
        float3 basis_t_ws;
        float3 basis_b_ws;
        BuildOrthonormalBasis(probe_normal_ws, basis_t_ws, basis_b_ws);

        const int2 probe_atlas_local_pos = int2(local_probe_texel_index % ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION, local_probe_texel_index / ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION);
        const int2 atlas_texel_pos = probe_tile_id * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION + probe_atlas_local_pos;
        const float4 probe_value = AdaptiveScreenSpaceProbeTex.Load(int3(atlas_texel_pos, 0));
        const float4 packed_sample = float4(probe_value.a, probe_value.rgb);

        const float2 oct_uv = (float2(probe_atlas_local_pos) + 0.5) * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
        const float3 dir_ws = SspDecodeDirByNormal(oct_uv, basis_t_ws, basis_b_ws, probe_normal_ws);
        const float4 sh_basis = EvaluateL1ShBasis(dir_ws);
        packed_sh_coeff0 = packed_sample * sh_basis.x;
        packed_sh_coeff1 = packed_sample * sh_basis.y;
        packed_sh_coeff2 = packed_sample * sh_basis.z;
        packed_sh_coeff3 = packed_sample * sh_basis.w;
        }
    }

    gs_packed_sh_coeff0[gindex] = packed_sh_coeff0;
    gs_packed_sh_coeff1[gindex] = packed_sh_coeff1;
    gs_packed_sh_coeff2[gindex] = packed_sh_coeff2;
    gs_packed_sh_coeff3[gindex] = packed_sh_coeff3;

    GroupMemoryBarrierWithGroupSync();

    const uint probe_base_index = probe_group_local_index * ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT;
    [unroll]
    for(uint reduce_step = ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT >> 1u; reduce_step > 0u; reduce_step >>= 1u)
    {
        if(local_probe_lane < reduce_step)
        {
            const uint dst_index = probe_base_index + local_probe_lane;
            const uint src_index = dst_index + reduce_step;
            gs_packed_sh_coeff0[dst_index] += gs_packed_sh_coeff0[src_index];
            gs_packed_sh_coeff1[dst_index] += gs_packed_sh_coeff1[src_index];
            gs_packed_sh_coeff2[dst_index] += gs_packed_sh_coeff2[src_index];
            gs_packed_sh_coeff3[dst_index] += gs_packed_sh_coeff3[src_index];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if(local_probe_lane != 0u)
    {
        return;
    }

    if(!is_probe_tile_in_range)
    {
        return;
    }

    if(!is_probe_tile_valid)
    {
        AsspStoreZeroPackedSh(probe_tile_id, logical_sh_tex_size);
        return;
    }

#if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
    const float texel_solid_angle = (2.0 * 3.14159265359) / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#else
    const float texel_solid_angle = (4.0 * 3.14159265359) / float(ADAPTIVE_SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#endif
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 0, logical_sh_tex_size)] = gs_packed_sh_coeff0[probe_base_index] * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 1, logical_sh_tex_size)] = gs_packed_sh_coeff1[probe_base_index] * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 2, logical_sh_tex_size)] = gs_packed_sh_coeff2[probe_base_index] * texel_solid_angle;
    RWAdaptiveScreenSpaceProbePackedSHTex[AsspPackedShAtlasTexelCoord(probe_tile_id, 3, logical_sh_tex_size)] = gs_packed_sh_coeff3[probe_base_index] * texel_solid_angle;
}

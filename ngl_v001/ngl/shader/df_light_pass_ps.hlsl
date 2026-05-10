
/*

	df_light_pass_ps.hlsl
 
 */

#include "include/brdf.hlsli"
#include "include/math_util.hlsli"
#include "include/rand_util.hlsli"

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};

#include "include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
ConstantBuffer<SceneDirectionalShadowSampleInfo> cb_ngl_shadowview;

struct CbLightingPass
{
	int enable_feedback_blur_test;
	int is_first_frame;

    float d_lit_intensity;
    float sky_lit_intensity;

    int gi_sample_mode;
    int is_enable_sky_visibility;
    int is_enable_radiance;
    int dbg_view_srvs_sky_visibility;
    float probe_sample_offset_view;
    float probe_sample_offset_surface_normal;
    float probe_sample_offset_bent_normal;
    float _pad_cb_lighting_pass0;
};
ConstantBuffer<CbLightingPass> cb_ngl_lighting_pass;

// GI source selection:
//   none = probe GI を使わない
//   ssp  = Screen Space Probe を使う
//   fsp  = Frustum Space Probe を使う
//   assp = Adaptive Screen Space Probe を使う
static const int k_gi_sample_mode_none = 0;
static const int k_gi_sample_mode_ssp = 1;
static const int k_gi_sample_mode_fsp = 2;
static const int k_gi_sample_mode_assp = 3;
static const int k_gi_sample_mode_ddgi = 4;

Texture2D tex_lineardepth;// Linear View Depth.
Texture2D tex_gbuffer0;
Texture2D tex_gbuffer1;
Texture2D tex_gbuffer2;
Texture2D tex_gbuffer3;
Texture2D tex_prev_light;
Texture2D tex_shadowmap;
Texture2D tex_ssao;// rgb:bent normal, a:AO.
Texture2D tex_bent_normal;// BentNormalテクスチャ.

SamplerState samp;
SamplerComparisonState samp_shadow;

TextureCube tex_ibl_diffuse;
TextureCube tex_ibl_specular;
Texture2D tex_ibl_dfg;

// GI
#include "srvs/srvs_util.hlsli"
#include "srvs/assp/assp_probe_common.hlsli"


// DirectionalLight評価. 標準.
void EvalDirectionalLightStandard
(
	out float3 out_diffuse, out float3 out_specular,

	float3 light_intensity, float3 L, 
	float3 N, float3 V,
	float3 base_color, float roughness, float metalness
)
{
	const float3 diffuse_term = brdf_lambert(base_color, roughness, metalness, N, V, L);
	const float3 specular_term = brdf_standard_ggx(base_color, roughness, metalness, N, V, L);
    
	const float cos_term = saturate(dot(N, L));

	out_diffuse = cos_term * diffuse_term * light_intensity;
	out_specular = cos_term * specular_term * light_intensity;
}

// IBL評価. 標準.
void EvalIblDiffuseStandard
(
	out float3 out_diffuse, out float3 out_specular,

	TextureCube tex_cube_ibl_diffuse, TextureCube tex_cube_ibl_spacular, Texture2D tex_2d_specular_dfg, SamplerState samp, 
	float3 N, float3 V, 
	float3 base_color, float roughness, float metalness
)
{
	const float3 L_Reflect = 2 * dot( V, N ) * N - V;
	const float3 F = brdf_schlick_roughness_F(compute_F0_default(base_color, metalness), roughness, N, V, L_Reflect);// Roughnessを考慮したFresnel

	const float3 brdf_diffuse = brdf_lambert(base_color, roughness, metalness, N, V, L_Reflect);// diffuse BRDF.

	// Roughnessによる読み取りMipの計算.
	uint ibl_spec_miplevel, ibl_spec_width, ibl_spec_height, ibl_spec_mipcount;
	tex_cube_ibl_spacular.GetDimensions(ibl_spec_miplevel, ibl_spec_width, ibl_spec_height, ibl_spec_mipcount);
	const float ibl_specular_mip = (float)(ibl_spec_mipcount-1.0) * roughness;

	const float3 irradiance_specular = tex_cube_ibl_spacular.SampleLevel(samp, L_Reflect, ibl_specular_mip).rgb;
	const float4 specular_dfg = tex_2d_specular_dfg.SampleLevel(samp, float2(saturate(dot(N, V)), roughness), 0);
	const float3 irradiance_diffuse = tex_cube_ibl_diffuse.SampleLevel(samp, N, 0).rgb;

	// FresnelでDiffuseとSpecularに分配.
	out_diffuse = brdf_diffuse * irradiance_diffuse;
	out_specular = irradiance_specular * (F * specular_dfg.x + specular_dfg.y);
}

void CalcSsProbeShUpsampleInfo(
    out int2 out_ss_probe_sh_base_texel,
    out float4 out_upscale_gathered_weight,
    out bool out_valid_upscale_sample,
    float2 screen_uv,
    float ld)
{
    const int2 ss_probe_sh_tex_size = SspPackedShAtlasLogicalResolution();
    const float2 ss_probe_sh_tex_size_f = float2(ss_probe_sh_tex_size);

    const float2 ss_probe_sh_texel_pos_f = screen_uv * ss_probe_sh_tex_size_f;
    out_ss_probe_sh_base_texel = clamp(int2(floor(ss_probe_sh_texel_pos_f - 0.5)), int2(0, 0), int2(ss_probe_sh_tex_size) - int2(2, 2));

    const float4 low_hw_depth4 = ScreenSpaceProbeTileInfoTex.GatherRed(samp, screen_uv);
    out_upscale_gathered_weight = float4(0, 0, 0, 0);
    const float upscale_limit_view_z = 0.01 + 0.25 * ld;
    for(int ci = 0; ci < 4; ++ci)
    {
        const float low_view_z = calc_view_z_from_ndc_z(low_hw_depth4[ci], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
        const float diff = abs(low_view_z - ld);
        out_upscale_gathered_weight[ci] = (diff < upscale_limit_view_z)? 1.0 - diff / upscale_limit_view_z : 0.0;
    }

    const float upscale_weight_sum = out_upscale_gathered_weight.x + out_upscale_gathered_weight.y + out_upscale_gathered_weight.z + out_upscale_gathered_weight.w;
    out_valid_upscale_sample = (upscale_weight_sum > 0.0);
    if(out_valid_upscale_sample)
    {
        out_upscale_gathered_weight /= upscale_weight_sum;
    }
}

float4 SampleSsProbePackedShCoeff(
    uint coeff_index,
    float2 screen_uv,
    int2 ss_probe_sh_base_texel,
    float4 upscale_gathered_weight,
    bool valid_upscale_sample)
{
    const int2 logical_resolution = SspPackedShAtlasLogicalResolution();
    float4 gathered_sh[4];
    gathered_sh[0] = ScreenSpaceProbePackedSHTex.Load(int3(SspPackedShAtlasTexelCoord(ss_probe_sh_base_texel + GatherComponentIndexToTexelOffset(0), coeff_index, logical_resolution), 0));
    gathered_sh[1] = ScreenSpaceProbePackedSHTex.Load(int3(SspPackedShAtlasTexelCoord(ss_probe_sh_base_texel + GatherComponentIndexToTexelOffset(1), coeff_index, logical_resolution), 0));
    gathered_sh[2] = ScreenSpaceProbePackedSHTex.Load(int3(SspPackedShAtlasTexelCoord(ss_probe_sh_base_texel + GatherComponentIndexToTexelOffset(2), coeff_index, logical_resolution), 0));
    gathered_sh[3] = ScreenSpaceProbePackedSHTex.Load(int3(SspPackedShAtlasTexelCoord(ss_probe_sh_base_texel + GatherComponentIndexToTexelOffset(3), coeff_index, logical_resolution), 0));

    if(valid_upscale_sample)
    {
        return
            gathered_sh[0] * upscale_gathered_weight[0]
            + gathered_sh[1] * upscale_gathered_weight[1]
            + gathered_sh[2] * upscale_gathered_weight[2]
            + gathered_sh[3] * upscale_gathered_weight[3];
    }

    uint2 packed_sh_tex_size;
    ScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const float2 logical_resolution_f = float2(logical_resolution);
    const float2 packed_sh_tex_size_f = float2(packed_sh_tex_size);
    const float2 coeff_offset = float2(SspPackedShAtlasCoeffOffset(coeff_index, logical_resolution));
    const float2 coeff_uv = (clamp(screen_uv * logical_resolution_f, 0.5, logical_resolution_f - 0.5) + coeff_offset) / packed_sh_tex_size_f;
    return ScreenSpaceProbePackedSHTex.SampleLevel(samp, coeff_uv, 0);
}

struct SsProbePackedShL1Sample
{
    float4 sky_visibility_sh;
    float4 radiance_sh_r;
    float4 radiance_sh_g;
    float4 radiance_sh_b;
};

SsProbePackedShL1Sample SampleSsProbePackedShL1(
    float2 screen_uv,
    int2 ss_probe_sh_base_texel,
    float4 upscale_gathered_weight,
    bool valid_upscale_sample)
{
    SsProbePackedShL1Sample result;

    const float4 coeff0 = SampleSsProbePackedShCoeff(0, screen_uv, ss_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff1 = SampleSsProbePackedShCoeff(1, screen_uv, ss_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff2 = SampleSsProbePackedShCoeff(2, screen_uv, ss_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff3 = SampleSsProbePackedShCoeff(3, screen_uv, ss_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);

    result.sky_visibility_sh = float4(coeff0.r, coeff1.r, coeff2.r, coeff3.r);
    result.radiance_sh_r = float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g);
    result.radiance_sh_g = float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b);
    result.radiance_sh_b = float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a);

    return result;
}

float4 ConvolveSsProbeRadianceL1ToDiffuseIrradiance(float4 radiance_sh_coeff)
{
    return ConvolveL1ShByClampedCosine(radiance_sh_coeff);
}

float4 ConvolveSsProbeSkyVisibilityL1ToIblOcclusion(float4 sky_visibility_sh_coeff)
{
    return ConvolveL1ShByNormalizedClampedCosine(sky_visibility_sh_coeff);
}

float3 EvalSsProbeRadianceL1DiffuseIrradiance(SsProbePackedShL1Sample ss_probe_sh, float4 sh_basis)
{
    // Packed SH stores plain radiance. Apply cosine convolution here so the evaluated value is diffuse irradiance.
    return float3(
        dot(ConvolveSsProbeRadianceL1ToDiffuseIrradiance(ss_probe_sh.radiance_sh_r), sh_basis),
        dot(ConvolveSsProbeRadianceL1ToDiffuseIrradiance(ss_probe_sh.radiance_sh_g), sh_basis),
        dot(ConvolveSsProbeRadianceL1ToDiffuseIrradiance(ss_probe_sh.radiance_sh_b), sh_basis));
}

float EvalSsProbeSkyVisibilityL1IblOcclusion(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    // Sky visibility is stored as a plain directional function. Use normalized cosine convolution for diffuse-style IBL occlusion.
    return dot(ConvolveSsProbeSkyVisibilityL1ToIblOcclusion(sky_visibility_sh_coeff), sh_basis);
}

float EvalSsProbeSkyVisibilityL1Directional(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    // Plain directional query used for specular-side visibility before roughness-based stabilization.
    return dot(sky_visibility_sh_coeff, sh_basis);
}

struct FspProbePackedShL1Sample
{
    float4 sky_visibility_sh;
    float4 radiance_sh_r;
    float4 radiance_sh_g;
    float4 radiance_sh_b;
};

// FSP SH のゼロ値を返す簡易コンストラクタ。
FspProbePackedShL1Sample MakeZeroFspProbePackedShL1Sample()
{
    FspProbePackedShL1Sample result;
    result.sky_visibility_sh = 0.0.xxxx;
    result.radiance_sh_r = 0.0.xxxx;
    result.radiance_sh_g = 0.0.xxxx;
    result.radiance_sh_b = 0.0.xxxx;
    return result;
}

// SH パケットを重み付きで加算する。
void AccumulateFspPackedShL1Sample(inout FspProbePackedShL1Sample accum, FspProbePackedShL1Sample sample_value, float weight)
{
    accum.sky_visibility_sh += sample_value.sky_visibility_sh * weight;
    accum.radiance_sh_r += sample_value.radiance_sh_r * weight;
    accum.radiance_sh_g += sample_value.radiance_sh_g * weight;
    accum.radiance_sh_b += sample_value.radiance_sh_b * weight;
}

// SH パケット全体へ一様スケールを掛ける。
void ScaleFspPackedShL1Sample(inout FspProbePackedShL1Sample sample_value, float scale)
{
    sample_value.sky_visibility_sh *= scale;
    sample_value.radiance_sh_r *= scale;
    sample_value.radiance_sh_g *= scale;
    sample_value.radiance_sh_b *= scale;
}

// cell -> probe の対応から packed SH を 1 probe 分ロードする。
bool FspTryLoadPackedShL1FromCellIndex(out FspProbePackedShL1Sample result, uint global_cell_index)
{
    result = MakeZeroFspProbePackedShL1Sample();

    const uint probe_index = FspCellProbeIndexBuffer[global_cell_index];
    if(probe_index == k_fsp_invalid_probe_index || probe_index >= (uint)cb_srvs.fsp_probe_pool_size)
    {
        return false;
    }

    const FspProbePoolData probe_pool_data = FspProbePoolBuffer[probe_index];
    if(0 == (probe_pool_data.flags & k_fsp_probe_flag_allocated) || probe_pool_data.owner_cell_index != global_cell_index)
    {
        return false;
    }

    const int2 probe_tile_id = int2(FspProbeAtlasMapPos(probe_index));
    const float4 coeff0 = FspPackedShAtlasLoadCoeff(probe_tile_id, 0);
    const float4 coeff1 = FspPackedShAtlasLoadCoeff(probe_tile_id, 1);
    const float4 coeff2 = FspPackedShAtlasLoadCoeff(probe_tile_id, 2);
    const float4 coeff3 = FspPackedShAtlasLoadCoeff(probe_tile_id, 3);
    result.sky_visibility_sh = float4(
        coeff0.r,
        coeff1.r,
        coeff2.r,
        coeff3.r);
    result.radiance_sh_r = float4(
        coeff0.g,
        coeff1.g,
        coeff2.g,
        coeff3.g);
    result.radiance_sh_g = float4(
        coeff0.b,
        coeff1.b,
        coeff2.b,
        coeff3.b);
    result.radiance_sh_b = float4(
        coeff0.a,
        coeff1.a,
        coeff2.a,
        coeff3.a);
    return true;
}

// fine/coarse cascade の境界帯で coarse へ逃がす確率を返す。
float FspCalcCascadeBoundaryDitherRate(float3 sample_pos_ws, uint cascade_index)
{
    if((cascade_index + 1u) >= FspCascadeCount())
    {
        return 0.0;
    }

    uint coarse_global_cell_index = k_fsp_invalid_probe_index;
    if(!FspTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, cascade_index + 1u, coarse_global_cell_index))
    {
        return 0.0;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const FspCascadeGridParam coarse_cascade = FspGetCascadeParam(cascade_index + 1u);
    const float3 cascade_max_pos = cascade.grid.grid_min_pos + float3(cascade.grid.grid_resolution) * cascade.grid.cell_size;
    const float3 dist_to_min = sample_pos_ws - cascade.grid.grid_min_pos;
    const float3 dist_to_max = cascade_max_pos - sample_pos_ws;
    const float boundary_dist = min(min(dist_to_min.x, dist_to_max.x), min(min(dist_to_min.y, dist_to_max.y), min(dist_to_min.z, dist_to_max.z)));
    const float dither_width = max(coarse_cascade.grid.cell_size, cascade.grid.cell_size);
    return 1.0 - saturate(boundary_dist / max(dither_width, 1e-5));
}

// ライティング時に使う cascade を 1 本だけ選ぶ。
bool FspTrySelectLightingCascade(out uint cascade_index, float3 sample_pos_ws, float2 dither_seed)
{
    cascade_index = 0;

    const uint cascade_count = FspCascadeCount();
    [loop]
    for(uint ci = 0; ci < cascade_count; ++ci)
    {
        uint global_cell_index = k_fsp_invalid_probe_index;
        if(!FspTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, ci, global_cell_index))
        {
            continue;
        }

        cascade_index = ci;
        // 境界帯だけ coarse cascade へ確率的に逃がし、ブレンドではなくディザで切り替える。
        const float coarse_select_rate = FspCalcCascadeBoundaryDitherRate(sample_pos_ws, ci);
        if(coarse_select_rate > 0.0 && interleaved_gradient_noise(dither_seed) < coarse_select_rate)
        {
            cascade_index = min(ci + 1u, cascade_count - 1u);
        }
        return true;
    }

    return false;
}

// 最初に見つかった有効 probe をそのまま使う nearest 参照。
bool TrySampleFspPackedShL1Nearest(out FspProbePackedShL1Sample result, float3 sample_pos_ws)
{
    result = MakeZeroFspProbePackedShL1Sample();

    const uint cascade_count = FspCascadeCount();
    [loop]
    for(uint cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
    {
        uint global_cell_index = k_fsp_invalid_probe_index;
        if(!FspTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, cascade_index, global_cell_index))
        {
            continue;
        }

        if(!FspTryLoadPackedShL1FromCellIndex(result, global_cell_index))
        {
            continue;
        }
        return true;
    }

    return false;
}

// use_stochastic_sampling は呼び出し側で UI/設定から決めて渡し、
// 補間関数自体は sampling policy のみを切り替える。
// 補間 ON 時は 8近傍の Trilinear 重みを求め、合成か stochastic 1-sample を選ぶ。
bool TrySampleFspPackedShL1Interpolated(out FspProbePackedShL1Sample result, float3 sample_pos_ws, float2 dither_seed, bool use_stochastic_sampling)
{
    result = MakeZeroFspProbePackedShL1Sample();

    uint cascade_index = 0;
    if(!FspTrySelectLightingCascade(cascade_index, sample_pos_ws, dither_seed))
    {
        return false;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 grid_coordf = (sample_pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv - float3(0.5, 0.5, 0.5);
    const int3 base_coord = int3(floor(grid_coordf));
    const float3 lerp_rate = saturate(frac(grid_coordf));

    float neighbor_weight[8];
    uint neighbor_cell_index[8];
    [unroll]
    for(uint i = 0; i < 8; ++i)
    {
        neighbor_weight[i] = 0.0;
        neighbor_cell_index[i] = k_fsp_invalid_probe_index;
    }

    float total_weight = 0.0;
    [unroll]
    for(int oz = 0; oz < 2; ++oz)
    {
        [unroll]
        for(int oy = 0; oy < 2; ++oy)
        {
            [unroll]
            for(int ox = 0; ox < 2; ++ox)
            {
                const int3 neighbor_coord = base_coord + int3(ox, oy, oz);
                if(any(neighbor_coord < 0) || any(neighbor_coord >= cascade.grid.grid_resolution))
                {
                    continue;
                }

                const float wx = (ox == 0) ? (1.0 - lerp_rate.x) : lerp_rate.x;
                const float wy = (oy == 0) ? (1.0 - lerp_rate.y) : lerp_rate.y;
                const float wz = (oz == 0) ? (1.0 - lerp_rate.z) : lerp_rate.z;
                const float base_weight = wx * wy * wz;
                if(base_weight <= 0.0)
                {
                    continue;
                }

                // Trilinear 基本重みに、probe 未配置セルを 0 に落とす追加 weighting を掛ける。
                const int3 neighbor_coord_toroidal = voxel_coord_toroidal_mapping(neighbor_coord, cascade.grid.grid_toroidal_offset, cascade.grid.grid_resolution);
                const uint local_cell_index = voxel_coord_to_index(neighbor_coord_toroidal, cascade.grid.grid_resolution);
                const uint global_cell_index = cascade.cell_offset + local_cell_index;

                const uint neighbor_index = uint(ox + oy * 2 + oz * 4);
                FspProbePackedShL1Sample probe_sh = MakeZeroFspProbePackedShL1Sample();
                if(!FspTryLoadPackedShL1FromCellIndex(probe_sh, global_cell_index))
                {
                    continue;
                }

                neighbor_weight[neighbor_index] = base_weight;
                neighbor_cell_index[neighbor_index] = global_cell_index;
                total_weight += base_weight;
            }
        }
    }

    if(total_weight <= 0.0)
    {
        return false;
    }

    if(use_stochastic_sampling)
    {
        // RayGuiding と同様に CDF を作り、Trilinear 重みを PDF として 1 サンプルだけ選ぶ。
        float neighbor_cdf[8];
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0; i < 8; ++i)
        {
            cdf_sum += neighbor_weight[i];
            neighbor_cdf[i] = cdf_sum;
        }

        const float cdf_sum_inv = rcp(cdf_sum);
        [unroll]
        for(uint i = 0; i < 8; ++i)
        {
            neighbor_cdf[i] *= cdf_sum_inv;
        }

        RandomInstance rng;
        rng.rngState = asuint(noise_float_to_float(float3(dither_seed + float2(17.0, 43.0), cb_srvs.frame_count)));
        const float guiding_rand = rng.rand();
        uint selected_index = 0;
        [unroll]
        for(uint i = 0; i < 8; ++i)
        {
            if(neighbor_weight[i] > 0.0)
            {
                selected_index = i;
                break;
            }
        }
        [unroll]
        for(uint i = 0; i < 8; ++i)
        {
            if(guiding_rand <= neighbor_cdf[i])
            {
                selected_index = i;
                break;
            }
        }

        // 確率的モードでは 8近傍を合成せず、CDF で選ばれた 1 セルだけを使う。
        return FspTryLoadPackedShL1FromCellIndex(result, neighbor_cell_index[selected_index]);
    }

    [unroll]
    for(uint i = 0; i < 8; ++i)
    {
        if(neighbor_weight[i] <= 0.0)
        {
            continue;
        }

        FspProbePackedShL1Sample probe_sh = MakeZeroFspProbePackedShL1Sample();
        if(!FspTryLoadPackedShL1FromCellIndex(probe_sh, neighbor_cell_index[i]))
        {
            continue;
        }

        AccumulateFspPackedShL1Sample(result, probe_sh, neighbor_weight[i]);
    }

    if(total_weight > 0.0)
    {
        ScaleFspPackedShL1Sample(result, rcp(total_weight));
        return true;
    }

    return false;
}

// FSP ライティングの入口。nearest / interpolated をここで切り替える。
bool TrySampleFspPackedShL1(out FspProbePackedShL1Sample result, float3 sample_pos_ws, float2 dither_seed, bool use_stochastic_sampling)
{
    if(0 != cb_srvs.fsp_lighting_interpolation_enable)
    {
        return TrySampleFspPackedShL1Interpolated(result, sample_pos_ws, dither_seed, use_stochastic_sampling);
    }
    return TrySampleFspPackedShL1Nearest(result, sample_pos_ws);
}

float3 EvalFspRadianceL1DiffuseIrradiance(FspProbePackedShL1Sample fsp_probe_sh, float4 sh_basis)
{
    return float3(
        dot(ConvolveL1ShByClampedCosine(fsp_probe_sh.radiance_sh_r), sh_basis),
        dot(ConvolveL1ShByClampedCosine(fsp_probe_sh.radiance_sh_g), sh_basis),
        dot(ConvolveL1ShByClampedCosine(fsp_probe_sh.radiance_sh_b), sh_basis));
}

float EvalFspSkyVisibilityL1IblOcclusion(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    return dot(ConvolveL1ShByNormalizedClampedCosine(sky_visibility_sh_coeff), sh_basis);
}

float EvalFspSkyVisibilityL1Directional(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    return dot(sky_visibility_sh_coeff, sh_basis);
}

struct DdgiProbePackedShL1Sample
{
    float4 sky_visibility_sh;
    float4 radiance_sh_r;
    float4 radiance_sh_g;
    float4 radiance_sh_b;
};

DdgiProbePackedShL1Sample MakeZeroDdgiProbePackedShL1Sample()
{
    DdgiProbePackedShL1Sample result;
    result.sky_visibility_sh = 0.0.xxxx;
    result.radiance_sh_r = 0.0.xxxx;
    result.radiance_sh_g = 0.0.xxxx;
    result.radiance_sh_b = 0.0.xxxx;
    return result;
}

void AccumulateDdgiPackedShL1Sample(inout DdgiProbePackedShL1Sample accum, DdgiProbePackedShL1Sample sample_value, float weight)
{
    accum.sky_visibility_sh += sample_value.sky_visibility_sh * weight;
    accum.radiance_sh_r += sample_value.radiance_sh_r * weight;
    accum.radiance_sh_g += sample_value.radiance_sh_g * weight;
    accum.radiance_sh_b += sample_value.radiance_sh_b * weight;
}

void ScaleDdgiPackedShL1Sample(inout DdgiProbePackedShL1Sample sample_value, float scale)
{
    sample_value.sky_visibility_sh *= scale;
    sample_value.radiance_sh_r *= scale;
    sample_value.radiance_sh_g *= scale;
    sample_value.radiance_sh_b *= scale;
}

bool DdgiTryLoadPackedShL1FromCellIndex(out DdgiProbePackedShL1Sample result, uint global_cell_index)
{
    result = MakeZeroDdgiProbePackedShL1Sample();
    if(global_cell_index >= (uint)cb_srvs.ddgi_total_cell_count)
    {
        return false;
    }

    const uint base_index = global_cell_index * 4;
    const float4 coeff0 = DdgiProbePackedShBuffer[base_index + 0];
    const float4 coeff1 = DdgiProbePackedShBuffer[base_index + 1];
    const float4 coeff2 = DdgiProbePackedShBuffer[base_index + 2];
    const float4 coeff3 = DdgiProbePackedShBuffer[base_index + 3];
    result.sky_visibility_sh = float4(coeff0.r, coeff1.r, coeff2.r, coeff3.r);
    result.radiance_sh_r = float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g);
    result.radiance_sh_g = float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b);
    result.radiance_sh_b = float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a);
    return true;
}

float DdgiEvalDistanceMomentVisibility(uint global_cell_index, float3 sample_pos_ws, float3 probe_pos_ws)
{
    if(0 == cb_srvs.ddgi_distance_weight_enable || global_cell_index >= (uint)cb_srvs.ddgi_total_cell_count)
    {
        return 1.0;
    }

    const float3 sample_to_probe_dir = normalize(probe_pos_ws - sample_pos_ws);
    const float4 dir_basis = EvaluateL1ShBasis(sample_to_probe_dir);
    const uint dist_base_index = global_cell_index * 8;
    const float4 mean_coeff0 = DdgiProbeDistanceMomentBuffer[dist_base_index + 0];
    const float4 mean_coeff1 = DdgiProbeDistanceMomentBuffer[dist_base_index + 1];
    const float4 mean_coeff2 = DdgiProbeDistanceMomentBuffer[dist_base_index + 2];
    const float4 mean_coeff3 = DdgiProbeDistanceMomentBuffer[dist_base_index + 3];
    const float4 mean2_coeff0 = DdgiProbeDistanceMomentBuffer[dist_base_index + 4];
    const float4 mean2_coeff1 = DdgiProbeDistanceMomentBuffer[dist_base_index + 5];
    const float4 mean2_coeff2 = DdgiProbeDistanceMomentBuffer[dist_base_index + 6];
    const float4 mean2_coeff3 = DdgiProbeDistanceMomentBuffer[dist_base_index + 7];

    const float mean_distance = max(0.0, dot(float4(mean_coeff0.x, mean_coeff1.x, mean_coeff2.x, mean_coeff3.x), dir_basis));
    const float mean2_distance = max(0.0, dot(float4(mean2_coeff0.x, mean2_coeff1.x, mean2_coeff2.x, mean2_coeff3.x), dir_basis));
    const float variance = max(mean2_distance - mean_distance * mean_distance, cb_srvs.ddgi_visibility_variance_bias);
    const float distance_to_probe_norm = saturate(length(probe_pos_ws - sample_pos_ws) * rcp(max(cb_srvs.ddgi_distance_normalize_m, 1e-3)));
    const float delta = max(distance_to_probe_norm - mean_distance, 0.0);
    const float p_max = variance / (variance + delta * delta);
    return max(pow(saturate(p_max), max(cb_srvs.ddgi_visibility_sharpness, 1e-3)), cb_srvs.ddgi_visibility_min_weight);
}

float DdgiCalcCascadeBoundaryDitherRate(float3 sample_pos_ws, uint cascade_index)
{
    if((cascade_index + 1u) >= DdgiCascadeCount())
    {
        return 0.0;
    }

    uint coarse_global_cell_index = k_fsp_invalid_probe_index;
    if(!DdgiTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, cascade_index + 1u, coarse_global_cell_index))
    {
        return 0.0;
    }

    const FspCascadeGridParam cascade = DdgiGetCascadeParam(cascade_index);
    const FspCascadeGridParam coarse_cascade = DdgiGetCascadeParam(cascade_index + 1u);
    const float3 cascade_max_pos = cascade.grid.grid_min_pos + float3(cascade.grid.grid_resolution) * cascade.grid.cell_size;
    const float3 dist_to_min = sample_pos_ws - cascade.grid.grid_min_pos;
    const float3 dist_to_max = cascade_max_pos - sample_pos_ws;
    const float boundary_dist = min(min(dist_to_min.x, dist_to_max.x), min(min(dist_to_min.y, dist_to_max.y), min(dist_to_min.z, dist_to_max.z)));
    const float dither_width = max(coarse_cascade.grid.cell_size, cascade.grid.cell_size);
    return 1.0 - saturate(boundary_dist / max(dither_width, 1e-5));
}

bool DdgiTrySelectLightingCascade(out uint cascade_index, float3 sample_pos_ws, float2 dither_seed)
{
    cascade_index = 0;
    const uint cascade_count = DdgiCascadeCount();
    [loop]
    for(uint ci = 0; ci < cascade_count; ++ci)
    {
        uint global_cell_index = k_fsp_invalid_probe_index;
        if(!DdgiTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, ci, global_cell_index))
        {
            continue;
        }
        cascade_index = ci;
        const float coarse_select_rate = DdgiCalcCascadeBoundaryDitherRate(sample_pos_ws, ci);
        if(coarse_select_rate > 0.0 && interleaved_gradient_noise(dither_seed) < coarse_select_rate)
        {
            cascade_index = min(ci + 1u, cascade_count - 1u);
        }
        return true;
    }
    return false;
}

bool TrySampleDdgiPackedShL1(out DdgiProbePackedShL1Sample result, float3 sample_pos_ws, float2 dither_seed)
{
    result = MakeZeroDdgiProbePackedShL1Sample();

    uint cascade_index = 0;
    if(!DdgiTrySelectLightingCascade(cascade_index, sample_pos_ws, dither_seed))
    {
        return false;
    }

    const FspCascadeGridParam cascade = DdgiGetCascadeParam(cascade_index);
    if(0 == cb_srvs.ddgi_lighting_interpolation_enable)
    {
        uint nearest_global_cell_index = k_fsp_invalid_probe_index;
        if(!DdgiTryGetGlobalCellIndexFromWorldPos(sample_pos_ws, cascade_index, nearest_global_cell_index))
        {
            return false;
        }
        return DdgiTryLoadPackedShL1FromCellIndex(result, nearest_global_cell_index);
    }

    const float3 grid_coordf = (sample_pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv - float3(0.5, 0.5, 0.5);
    const int3 base_coord = int3(floor(grid_coordf));
    const float3 lerp_rate = saturate(frac(grid_coordf));

    float neighbor_weight[8];
    uint neighbor_cell_index[8];
    [unroll]
    for(uint i = 0; i < 8; ++i)
    {
        neighbor_weight[i] = 0.0;
        neighbor_cell_index[i] = k_fsp_invalid_probe_index;
    }

    float total_weight = 0.0;
    [unroll]
    for(int oz = 0; oz < 2; ++oz)
    {
        [unroll]
        for(int oy = 0; oy < 2; ++oy)
        {
            [unroll]
            for(int ox = 0; ox < 2; ++ox)
            {
                const int3 neighbor_coord = base_coord + int3(ox, oy, oz);
                if(any(neighbor_coord < 0) || any(neighbor_coord >= cascade.grid.grid_resolution))
                {
                    continue;
                }

                const float wx = (ox == 0) ? (1.0 - lerp_rate.x) : lerp_rate.x;
                const float wy = (oy == 0) ? (1.0 - lerp_rate.y) : lerp_rate.y;
                const float wz = (oz == 0) ? (1.0 - lerp_rate.z) : lerp_rate.z;
                float weight = wx * wy * wz;
                if(weight <= 0.0)
                {
                    continue;
                }

                const int3 neighbor_coord_toroidal = voxel_coord_toroidal_mapping(neighbor_coord, cascade.grid.grid_toroidal_offset, cascade.grid.grid_resolution);
                const uint local_cell_index = voxel_coord_to_index(neighbor_coord_toroidal, cascade.grid.grid_resolution);
                const uint global_cell_index = cascade.cell_offset + local_cell_index;
                const float3 probe_pos_ws = DdgiCalcCellCenterWs(cascade_index, local_cell_index);
                weight *= DdgiEvalDistanceMomentVisibility(global_cell_index, sample_pos_ws, probe_pos_ws);

                DdgiProbePackedShL1Sample probe_sh = MakeZeroDdgiProbePackedShL1Sample();
                if(!DdgiTryLoadPackedShL1FromCellIndex(probe_sh, global_cell_index))
                {
                    continue;
                }

                const uint neighbor_index = uint(ox + oy * 2 + oz * 4);
                neighbor_weight[neighbor_index] = weight;
                neighbor_cell_index[neighbor_index] = global_cell_index;
                total_weight += weight;
            }
        }
    }
    if(total_weight <= 0.0)
    {
        return false;
    }

    [unroll]
    for(uint i = 0; i < 8; ++i)
    {
        if(neighbor_weight[i] <= 0.0)
        {
            continue;
        }
        DdgiProbePackedShL1Sample probe_sh = MakeZeroDdgiProbePackedShL1Sample();
        if(!DdgiTryLoadPackedShL1FromCellIndex(probe_sh, neighbor_cell_index[i]))
        {
            continue;
        }
        AccumulateDdgiPackedShL1Sample(result, probe_sh, neighbor_weight[i]);
    }
    ScaleDdgiPackedShL1Sample(result, rcp(total_weight));
    return true;
}

float3 EvalDdgiRadianceL1DiffuseIrradiance(DdgiProbePackedShL1Sample probe_sh, float4 sh_basis)
{
    return float3(
        dot(ConvolveL1ShByClampedCosine(probe_sh.radiance_sh_r), sh_basis),
        dot(ConvolveL1ShByClampedCosine(probe_sh.radiance_sh_g), sh_basis),
        dot(ConvolveL1ShByClampedCosine(probe_sh.radiance_sh_b), sh_basis));
}

struct AsspProbePackedShL1Sample
{
    float4 sky_visibility_sh;
    float4 radiance_sh_r;
    float4 radiance_sh_g;
    float4 radiance_sh_b;
};

AsspProbePackedShL1Sample MakeZeroAsspProbePackedShL1Sample()
{
    AsspProbePackedShL1Sample result;
    result.sky_visibility_sh = 0.0.xxxx;
    result.radiance_sh_r = 0.0.xxxx;
    result.radiance_sh_g = 0.0.xxxx;
    result.radiance_sh_b = 0.0.xxxx;
    return result;
}

void CalcAsspProbeShUpsampleInfo(
    out int2 out_assp_probe_sh_base_texel,
    out float4 out_upscale_gathered_weight,
    out bool out_valid_upscale_sample,
    float2 screen_uv,
    float linear_depth)
{
    const int2 assp_probe_sh_tex_size = AsspPackedShAtlasLogicalResolution();
    const float2 assp_probe_sh_tex_size_f = float2(assp_probe_sh_tex_size);
    const float2 assp_probe_sh_texel_pos_f = screen_uv * assp_probe_sh_tex_size_f;
    out_assp_probe_sh_base_texel = clamp(int2(floor(assp_probe_sh_texel_pos_f - 0.5)), int2(0, 0), int2(assp_probe_sh_tex_size) - int2(2, 2));

    const float4 low_hw_depth4 = AdaptiveScreenSpaceProbeTileInfoTex.GatherRed(samp, screen_uv);
    out_upscale_gathered_weight = float4(0.0, 0.0, 0.0, 0.0);
    const float upscale_limit_view_z = 0.01 + 0.25 * linear_depth;
    [unroll]
    for(int ci = 0; ci < 4; ++ci)
    {
        if(!isValidDepth(low_hw_depth4[ci]))
        {
            out_upscale_gathered_weight[ci] = 0.0;
            continue;
        }
        const float low_view_z = abs(calc_view_z_from_ndc_z(low_hw_depth4[ci], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef));
        const float diff = abs(low_view_z - linear_depth);
        out_upscale_gathered_weight[ci] = (diff < upscale_limit_view_z) ? (1.0 - diff / upscale_limit_view_z) : 0.0;
    }

    const float upscale_weight_sum =
        out_upscale_gathered_weight.x + out_upscale_gathered_weight.y + out_upscale_gathered_weight.z + out_upscale_gathered_weight.w;
    out_valid_upscale_sample = (upscale_weight_sum > 0.0);
    if(out_valid_upscale_sample)
    {
        out_upscale_gathered_weight /= upscale_weight_sum;
    }
}

float4 SampleAsspProbePackedShCoeff(
    uint coeff_index,
    float2 screen_uv,
    int2 assp_probe_sh_base_texel,
    float4 upscale_gathered_weight,
    bool valid_upscale_sample)
{
    const int2 logical_resolution = AsspPackedShAtlasLogicalResolution();
    float4 gathered_sh[4];
    gathered_sh[0] = AdaptiveScreenSpaceProbePackedSHTex.Load(int3(AsspPackedShAtlasTexelCoord(assp_probe_sh_base_texel + GatherComponentIndexToTexelOffset(0), coeff_index, logical_resolution), 0));
    gathered_sh[1] = AdaptiveScreenSpaceProbePackedSHTex.Load(int3(AsspPackedShAtlasTexelCoord(assp_probe_sh_base_texel + GatherComponentIndexToTexelOffset(1), coeff_index, logical_resolution), 0));
    gathered_sh[2] = AdaptiveScreenSpaceProbePackedSHTex.Load(int3(AsspPackedShAtlasTexelCoord(assp_probe_sh_base_texel + GatherComponentIndexToTexelOffset(2), coeff_index, logical_resolution), 0));
    gathered_sh[3] = AdaptiveScreenSpaceProbePackedSHTex.Load(int3(AsspPackedShAtlasTexelCoord(assp_probe_sh_base_texel + GatherComponentIndexToTexelOffset(3), coeff_index, logical_resolution), 0));

    if(valid_upscale_sample)
    {
        return
            gathered_sh[0] * upscale_gathered_weight[0]
            + gathered_sh[1] * upscale_gathered_weight[1]
            + gathered_sh[2] * upscale_gathered_weight[2]
            + gathered_sh[3] * upscale_gathered_weight[3];
    }

    uint2 packed_sh_tex_size;
    AdaptiveScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
    const float2 logical_resolution_f = float2(logical_resolution);
    const float2 packed_sh_tex_size_f = float2(packed_sh_tex_size);
    const float2 coeff_offset = float2(AsspPackedShAtlasCoeffOffset(coeff_index, logical_resolution));
    const float2 coeff_uv = (clamp(screen_uv * logical_resolution_f, 0.5, logical_resolution_f - 0.5) + coeff_offset) / packed_sh_tex_size_f;
    return AdaptiveScreenSpaceProbePackedSHTex.SampleLevel(samp, coeff_uv, 0);
}

AsspProbePackedShL1Sample SampleAsspPackedShL1(
    float2 screen_uv,
    int2 assp_probe_sh_base_texel,
    float4 upscale_gathered_weight,
    bool valid_upscale_sample)
{
    AsspProbePackedShL1Sample result;

    const float4 coeff0 = SampleAsspProbePackedShCoeff(0, screen_uv, assp_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff1 = SampleAsspProbePackedShCoeff(1, screen_uv, assp_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff2 = SampleAsspProbePackedShCoeff(2, screen_uv, assp_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);
    const float4 coeff3 = SampleAsspProbePackedShCoeff(3, screen_uv, assp_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample);

    result.sky_visibility_sh = float4(coeff0.r, coeff1.r, coeff2.r, coeff3.r);
    result.radiance_sh_r = float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g);
    result.radiance_sh_g = float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b);
    result.radiance_sh_b = float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a);
    return result;
}

bool TrySampleAsspPackedShL1(out AsspProbePackedShL1Sample result, float2 screen_uv, float linear_depth)
{
    result = MakeZeroAsspProbePackedShL1Sample();
    int2 assp_probe_sh_base_texel = int2(0, 0);
    float4 upscale_gathered_weight = float4(0.0, 0.0, 0.0, 0.0);
    bool valid_upscale_sample = false;
    CalcAsspProbeShUpsampleInfo(
        assp_probe_sh_base_texel,
        upscale_gathered_weight,
        valid_upscale_sample,
        screen_uv,
        linear_depth);
    result = SampleAsspPackedShL1(
        screen_uv,
        assp_probe_sh_base_texel,
        upscale_gathered_weight,
        valid_upscale_sample);
    return true;
}

float3 EvalAsspRadianceL1DiffuseIrradiance(AsspProbePackedShL1Sample assp_probe_sh, float4 sh_basis)
{
    return float3(
        dot(ConvolveL1ShByClampedCosine(assp_probe_sh.radiance_sh_r), sh_basis),
        dot(ConvolveL1ShByClampedCosine(assp_probe_sh.radiance_sh_g), sh_basis),
        dot(ConvolveL1ShByClampedCosine(assp_probe_sh.radiance_sh_b), sh_basis));
}

float EvalAsspSkyVisibilityL1IblOcclusion(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    return dot(ConvolveL1ShByNormalizedClampedCosine(sky_visibility_sh_coeff), sh_basis);
}

float EvalAsspSkyVisibilityL1Directional(float4 sky_visibility_sh_coeff, float4 sh_basis)
{
    return dot(sky_visibility_sh_coeff, sh_basis);
}

float EvalDirectionalShadow(Texture2D tex_cascade_shadowmap, SamplerComparisonState comp_samp, float3 L, float3 pixel_pos_ws, float3 normal, float3 view_origin, float3 camera_dir)
{
	// Cascade Index.
	const float view_depth = dot(pixel_pos_ws - view_origin, camera_dir);
	int sample_cascade_index = cb_ngl_shadowview.cb_valid_cascade_count - 1;
	for(int i = 0; i < cb_ngl_shadowview.cb_valid_cascade_count ; ++i)
	{
		if(view_depth < cb_ngl_shadowview.cb_cascade_far_distance4[i/4][i%4])
		{
			sample_cascade_index = i;
			break;
		}
	}

	
	// Shadowmap Sample.
	const float k_coef_constant_bias_ws = 0.01;
	const float k_coef_slope_bias_ws = 0.02;
	const float k_coef_normal_bias_ws = 0.03;
	const float k_cascade_blend_range_ws = 5.0;
	

	// 補間用の次のCascade.
	const int cascade_blend_count = max(2, cb_ngl_shadowview.cb_valid_cascade_count - sample_cascade_index);
	//const int cascade_blend_count = 1;// デバッグ用. ブレンドしない.

	// 近い方のCascadeの末端で, 次のCascadeとブレンドする.
	const float cascade_blend_rate =
		(1 < cascade_blend_count)? 1.0 - saturate((cb_ngl_shadowview.cb_cascade_far_distance4[sample_cascade_index/4][sample_cascade_index%4] - view_depth) / k_cascade_blend_range_ws) : 0.0;
	
	// ブレンド対象のCascade2つに関するLitVisibility. デフォルトは影なし(1.0).
	float2 cascade_blend_lit_sample = float2(1.0, 1.0);
	for(int cascade_i = 0; cascade_i < cascade_blend_count; ++cascade_i)
	{
		const int cascade_index = sample_cascade_index + cascade_i;
		const float cascade_size_rate_based_on_c0 = (cb_ngl_shadowview.cb_cascade_far_distance4[cascade_index/4][cascade_index%4]) / cb_ngl_shadowview.cb_cascade_far_distance4[0][0];
	
		const float slope_rate = (1.0 - saturate(dot(L, normal)));
		const float slope_bias_ws = k_coef_slope_bias_ws * slope_rate;
		const float normal_bias_ws = k_coef_normal_bias_ws * slope_rate;
		float shadow_sample_bias = max(k_coef_constant_bias_ws, slope_bias_ws);

		float3 shadow_sample_bias_vec_ws = (L * shadow_sample_bias) + (normal * normal_bias_ws);
		shadow_sample_bias_vec_ws *= cascade_size_rate_based_on_c0;// Cascadeのサイズによる補正項.
	
		const float3 pixel_pos_shadow_vs = mul(cb_ngl_shadowview.cb_shadow_view_mtx[cascade_index], float4(pixel_pos_ws + shadow_sample_bias_vec_ws, 1.0));
		const float4 pixel_pos_shadow_cs = mul(cb_ngl_shadowview.cb_shadow_proj_mtx[cascade_index], float4(pixel_pos_shadow_vs, 1.0));
		const float3 pixel_pos_shadow_cs_pd = pixel_pos_shadow_cs.xyz;;
		
		const float2 cascade_uv_lt = cb_ngl_shadowview.cb_cascade_tile_uvoffset_uvscale[cascade_index].xy;
		const float2 cascade_uv_size = cb_ngl_shadowview.cb_cascade_tile_uvoffset_uvscale[cascade_index].zw;
	
		float2 pixel_pos_shadow_uv = pixel_pos_shadow_cs_pd.xy * 0.5 + 0.5;
		pixel_pos_shadow_uv.y = 1.0 - pixel_pos_shadow_uv.y;// Y反転.
		// Atlas対応.
		pixel_pos_shadow_uv = pixel_pos_shadow_uv * cascade_uv_size + cascade_uv_lt;

		if(all(cascade_uv_lt < pixel_pos_shadow_uv) && all((cascade_uv_size+cascade_uv_lt) > pixel_pos_shadow_uv))
		{
			float shadow_comp_accum = 0.0;
#if 1
			// PCF.
			const int k_pcf_radius = 1;
			const int k_pcf_normalizer = (k_pcf_radius*2+1)*(k_pcf_radius*2+1);
			for(int oy = -k_pcf_radius; oy <= k_pcf_radius; ++oy)
				for(int ox = -k_pcf_radius; ox <= k_pcf_radius; ++ox)
					shadow_comp_accum += tex_cascade_shadowmap.SampleCmpLevelZero(comp_samp, pixel_pos_shadow_uv, pixel_pos_shadow_cs_pd.z, int2(ox, oy)).x;
		
			shadow_comp_accum = shadow_comp_accum / float(k_pcf_normalizer);
#else
			shadow_comp_accum = tex_cascade_shadowmap.SampleCmpLevelZero(comp_samp, pixel_pos_shadow_uv, pixel_pos_shadow_cs_pd.z, int2(0, 0)).x;
#endif
			
			cascade_blend_lit_sample[cascade_i] = shadow_comp_accum;
		}
	}

	// ブレンド.
	float light_visibility = lerp(cascade_blend_lit_sample[0], cascade_blend_lit_sample[1], cascade_blend_rate);
	
	return light_visibility;
}


uint2 calc_2d_position_from_index(uint index, uint tex_width)
{
    return uint2(index % tex_width, index / tex_width);
}
uint2 calc_probe_octahedral_map_atlas_texel_base_pos(uint index, uint tex_width)
{
    return calc_2d_position_from_index(index, tex_width) * k_fsp_probe_octmap_width;
}

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{	
    const float2 screen_uv = input.uv;
	// リニアView深度.
	const float ld = tex_lineardepth.SampleLevel(samp, screen_uv, 0).r;// LightingBufferとGBufferが同じ解像度前提でLoad.
	if(65535.0 <= ld)
	{
		discard;
	}
	
    // 他のhw_depthと単位を合わせて計算する場合用.
    const float hw_depth = calc_ndc_z_from_view_z(ld, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
	// LightingBufferとGBufferが同じ解像度前提でLoad.
	const float4 gb0 = tex_gbuffer0.SampleLevel(samp, screen_uv, 0);
	const float4 gb1 = tex_gbuffer1.SampleLevel(samp, screen_uv, 0);
	const float4 gb2 = tex_gbuffer2.SampleLevel(samp, screen_uv, 0);
	const float4 gb3 = tex_gbuffer3.SampleLevel(samp, screen_uv, 0);
    const float4 ssao_sample = tex_ssao.SampleLevel(samp, screen_uv, 0);
    const float4 bent_normal_sample = tex_bent_normal.SampleLevel(samp, screen_uv, 0);
	const float4 prev_light = tex_prev_light.SampleLevel(samp, screen_uv, 0);

	// GBuffer Decode.
	float3 gb_base_color = gb0.xyz;
	float gb_occlusion = gb0.w;
	float3 gb_normal_ws = normalize(gb1.xyz * 2.0 - 1.0);// gbufferからWorldNormalデコード.
	float gb_roughness = gb2.x;
	float gb_metalness = gb2.y;
	float gb_surface_option = gb2.z;
	float gb_material_id = gb2.w;
	float3 gb_emissive = gb3.xyz;

	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

	// ピクセルへのワールド空間レイを計算.
	const float3 to_pixel_ray_vs = CalcViewSpaceRay(input.uv, cb_ngl_sceneview.cb_proj_mtx);
	const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * ld, 1.0));
	const float3 to_pixel_vec_ws = pixel_pos_ws - view_origin;
	const float3 to_pixel_ray_ws = normalize(to_pixel_vec_ws);

	const float3 lit_intensity = float3(1.0, 1.0, 1.0) * cb_ngl_lighting_pass.d_lit_intensity;
	const float3 lit_dir = normalize(cb_ngl_shadowview.cb_shadow_view_inv_mtx[0]._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.

	const float3 V = -to_pixel_ray_ws;
	const float3 L = -lit_dir;
	
	// Directional Shadow.
	float light_visibility = EvalDirectionalShadow(tex_shadowmap, samp_shadow, L, pixel_pos_ws, gb_normal_ws, view_origin, camera_dir);
	float3 lit_color = (float3)0;

	// Directional Lit.
	{
		float3 dlit_diffuse, dlit_specular;
		EvalDirectionalLightStandard(dlit_diffuse, dlit_specular, lit_intensity, L, gb_normal_ws, V, gb_base_color, gb_roughness, gb_metalness);

		lit_color += (dlit_diffuse + dlit_specular) * light_visibility;
	}

	// GIのテスト.
	float diffuse_sky_visibility = 1.0;
    float specular_sky_visibility = 1.0;
    float3 gi_probe_diffuse_irradiance = float3(0.0, 0.0, 0.0);
    if(cb_ngl_lighting_pass.gi_sample_mode != k_gi_sample_mode_none
        && (cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.is_enable_radiance || cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility))
	{
        const float bent_normal_len_sq = dot(bent_normal_sample.xyz, bent_normal_sample.xyz);
        const float3 bent_normal_ws = (bent_normal_len_sq > 1e-6) ? (bent_normal_sample.xyz * rsqrt(bent_normal_len_sq)) : gb_normal_ws;
        const float3 gi_sample_pos_ws =
            pixel_pos_ws
            + (-to_pixel_ray_ws) * cb_ngl_lighting_pass.probe_sample_offset_view
            + gb_normal_ws * cb_ngl_lighting_pass.probe_sample_offset_surface_normal
            + bent_normal_ws * cb_ngl_lighting_pass.probe_sample_offset_bent_normal;

        // Diffuse uses normal-oriented SH evaluation, while specular uses reflection-oriented evaluation.
        const float4 sh_basis = EvaluateL1ShBasis(gb_normal_ws);
        const float3 reflected_view_dir = 2.0 * dot(V, gb_normal_ws) * gb_normal_ws - V;
        const float4 reflection_sh_basis = EvaluateL1ShBasis(reflected_view_dir);

        // gi_sample_mode に応じて、SSP か FSP のどちらを GI ソースに使うかを切り替える。
        if(cb_ngl_lighting_pass.gi_sample_mode == k_gi_sample_mode_ssp)
        {
            int2 ss_probe_sh_base_texel = int2(0, 0);
            float4 upscale_gathered_weight = float4(0.0, 0.0, 0.0, 0.0);
            bool valid_upscale_sample = false;
            CalcSsProbeShUpsampleInfo(ss_probe_sh_base_texel, upscale_gathered_weight, valid_upscale_sample, screen_uv, ld);

            const SsProbePackedShL1Sample ss_probe_sh = SampleSsProbePackedShL1(
                screen_uv,
                ss_probe_sh_base_texel,
                upscale_gathered_weight,
                valid_upscale_sample);
            if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility)
            {
                const float diffuse_sh_sample = max(0.0, EvalSsProbeSkyVisibilityL1IblOcclusion(ss_probe_sh.sky_visibility_sh, sh_basis));
                diffuse_sky_visibility = saturate(diffuse_sh_sample);

                const float directional_specular_sample = max(0.0, EvalSsProbeSkyVisibilityL1Directional(ss_probe_sh.sky_visibility_sh, reflection_sh_basis));
                const float roughness_blend = saturate(gb_roughness * gb_roughness);
                specular_sky_visibility = saturate(lerp(directional_specular_sample, diffuse_sky_visibility, roughness_blend));
            }
            if(cb_ngl_lighting_pass.is_enable_radiance)
            {
                gi_probe_diffuse_irradiance = max(
                    float3(0.0, 0.0, 0.0),
                    EvalSsProbeRadianceL1DiffuseIrradiance(ss_probe_sh, sh_basis));
            }
        }
        else if(cb_ngl_lighting_pass.gi_sample_mode == k_gi_sample_mode_fsp)
        {
            FspProbePackedShL1Sample fsp_probe_sh;
            // stochastic の有無はここで決め、sampling 関数には policy として渡すだけにする。
            const bool use_fsp_stochastic_sampling = (0 != cb_srvs.fsp_lighting_stochastic_sampling_enable);
            if(TrySampleFspPackedShL1(fsp_probe_sh, gi_sample_pos_ws, input.pos.xy, use_fsp_stochastic_sampling))
            {
                if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility)
                {
                    const float diffuse_sh_sample = max(0.0, EvalFspSkyVisibilityL1IblOcclusion(fsp_probe_sh.sky_visibility_sh, sh_basis));
                    diffuse_sky_visibility = saturate(diffuse_sh_sample);

                    const float directional_specular_sample = max(0.0, EvalFspSkyVisibilityL1Directional(fsp_probe_sh.sky_visibility_sh, reflection_sh_basis));
                    const float roughness_blend = saturate(gb_roughness * gb_roughness);
                    specular_sky_visibility = saturate(lerp(directional_specular_sample, diffuse_sky_visibility, roughness_blend));
                }
                if(cb_ngl_lighting_pass.is_enable_radiance)
                {
                    gi_probe_diffuse_irradiance = max(
                        float3(0.0, 0.0, 0.0),
                    EvalFspRadianceL1DiffuseIrradiance(fsp_probe_sh, sh_basis));
                }
            }
        }
        else if(cb_ngl_lighting_pass.gi_sample_mode == k_gi_sample_mode_assp)
        {
            AsspProbePackedShL1Sample assp_probe_sh;
            if(TrySampleAsspPackedShL1(assp_probe_sh, screen_uv, ld))
            {
                if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility)
                {
                    const float diffuse_sh_sample = max(0.0, EvalAsspSkyVisibilityL1IblOcclusion(assp_probe_sh.sky_visibility_sh, sh_basis));
                    diffuse_sky_visibility = saturate(diffuse_sh_sample);

                    const float directional_specular_sample = max(0.0, EvalAsspSkyVisibilityL1Directional(assp_probe_sh.sky_visibility_sh, reflection_sh_basis));
                    const float roughness_blend = saturate(gb_roughness * gb_roughness);
                    specular_sky_visibility = saturate(lerp(directional_specular_sample, diffuse_sky_visibility, roughness_blend));
                }
                if(cb_ngl_lighting_pass.is_enable_radiance)
                {
                    gi_probe_diffuse_irradiance = max(
                        float3(0.0, 0.0, 0.0),
                        EvalAsspRadianceL1DiffuseIrradiance(assp_probe_sh, sh_basis));
                }
            }
        }
        else if(cb_ngl_lighting_pass.gi_sample_mode == k_gi_sample_mode_ddgi)
        {
            DdgiProbePackedShL1Sample ddgi_probe_sh;
            if(TrySampleDdgiPackedShL1(ddgi_probe_sh, gi_sample_pos_ws, input.pos.xy))
            {
                if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility)
                {
                    const float diffuse_sh_sample = max(0.0, dot(ConvolveL1ShByNormalizedClampedCosine(ddgi_probe_sh.sky_visibility_sh), sh_basis));
                    diffuse_sky_visibility = saturate(diffuse_sh_sample);

                    const float directional_specular_sample = max(0.0, dot(ddgi_probe_sh.sky_visibility_sh, reflection_sh_basis));
                    const float roughness_blend = saturate(gb_roughness * gb_roughness);
                    specular_sky_visibility = saturate(lerp(directional_specular_sample, diffuse_sky_visibility, roughness_blend));
                }
                if(cb_ngl_lighting_pass.is_enable_radiance)
                {
                    gi_probe_diffuse_irradiance = max(
                        float3(0.0, 0.0, 0.0),
                        EvalDdgiRadianceL1DiffuseIrradiance(ddgi_probe_sh, sh_basis));
                }
            }
        }
	}
	
	// IBL.
	{
		float3 ibl_diffuse, ibl_specular;
		EvalIblDiffuseStandard(ibl_diffuse, ibl_specular, tex_ibl_diffuse, tex_ibl_specular, tex_ibl_dfg, samp, gb_normal_ws, V, gb_base_color, gb_roughness, gb_metalness);

        lit_color += ibl_diffuse * cb_ngl_lighting_pass.sky_lit_intensity * diffuse_sky_visibility * ssao_sample.a;
        // Approximate specular occlusion with reflection-direction visibility, then blend toward diffuse occlusion as roughness increases.
		lit_color += ibl_specular * cb_ngl_lighting_pass.sky_lit_intensity * specular_sky_visibility * ssao_sample.a;
        if(cb_ngl_lighting_pass.is_enable_radiance)
        {
            const float3 probe_diffuse_brdf = brdf_lambert(gb_base_color, gb_roughness, gb_metalness, gb_normal_ws, V, gb_normal_ws);
            lit_color += probe_diffuse_brdf * gi_probe_diffuse_irradiance * ssao_sample.a;
        }
	}


    // ------------------------------------------------------------------------------
        // NaNチェック.
        const float3  k_lit_nan_key_color = float3(1.0, 0.25, 1.0);
        if(isnan(lit_color.x) || isnan(lit_color.y) || isnan(lit_color.z))
        {
            // ライティング計算でNaN検出した場合はキーになるカラーをそのまま返す.
            return float4(k_lit_nan_key_color, 0.0);
        }
        // NaNチェック. 前回フレームのバッファがNaNキー色の場合は, そのまま返す.
        if(all(prev_light.rgb == k_lit_nan_key_color))
        {
            return float4(k_lit_nan_key_color, 0.0);
        }
    // ------------------------------------------------------------------------------

	// 過去フレームを使ったフィードバックブラーテスト.
	if(cb_ngl_lighting_pass.enable_feedback_blur_test)
	{
		// 画面端でテスト用のフィードバックブラー.
		const float2 dist_from_center = (input.uv - 0.5);
		const float length_from_center = length(dist_from_center);
		const float k_lenght_min = 0.4;
		float prev_blend_rate = saturate((length_from_center - k_lenght_min)*5.0);
		lit_color = lerp(lit_color, prev_light.rgb, prev_blend_rate * 0.95);
	}
    

    // デバッグ表示.
    // ------------------------------------------------------------------------------
        // sky_visibility のデバッグ表示. 最終ライティングを上書きして visibility のみを見る。
        if(cb_ngl_lighting_pass.dbg_view_srvs_sky_visibility)
        {
            lit_color = diffuse_sky_visibility;
        }
    // ------------------------------------------------------------------------------


	return float4(lit_color, 1.0);
}

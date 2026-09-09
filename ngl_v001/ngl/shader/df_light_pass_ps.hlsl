
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
    int dbg_view_instant_rdv_sky_visibility;
    float probe_sample_offset_view;
    float probe_sample_offset_surface_normal;
    float probe_sample_offset_bent_normal;
    float _pad_cb_lighting_pass0;
};
ConstantBuffer<CbLightingPass> cb_ngl_lighting_pass;

// GI source selection:
//   none = probe GI を使わない
//   fsp  = Frustum Space Probe を使う
//   assp = Adaptive Screen Space Probe を使う
static const int k_gi_sample_mode_none = 0;
static const int k_gi_sample_mode_fsp = 2;
static const int k_gi_sample_mode_assp = 3;

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
#include "instant_rdv/instant_rdv_util.hlsli"
#include "instant_rdv/assp/assp_probe_common.hlsli"


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

// Dense IrradianceVolume は全セルが有効SHを持つ前提で、final shading hot pathではvalidity判定をしない。
FspProbePackedShL1Sample FspLoadPackedShL1FromCellIndexUnchecked(uint irradiance_volume_cell_index)
{
    FspProbePackedShL1Sample result;
    const float4 coeff0 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 0);
    const float4 coeff1 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 1);
    const float4 coeff2 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 2);
    const float4 coeff3 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 3);
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
    return result;
}

bool FspIsWorldPosInsideDenseIrradianceVolumeCascade(float3 sample_pos_ws, uint cascade_index)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 grid_coordf = (sample_pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv;
    return all(grid_coordf >= 0.0.xxx) && all(grid_coordf < float3(cascade.grid.grid_resolution));
}

// グリッドの量子化により、実際の中心は各軸で1セル未満ずれる。
// カメラの連続位置を中心とする選択範囲を1セル分内側に縮め、実グリッド内に収める。
// CPUがゼロ方向に切り捨てる負のカメラ座標でも、この余白でずれを吸収する。
float FspDenseIrradianceVolumeSelectionHalfExtent(uint cascade_index)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    return (float(cascade.grid.grid_resolution.x) * 0.5 - 1.0) * cascade.grid.cell_size;
}

float FspDenseIrradianceVolumeCameraDistance(float3 sample_pos_ws)
{
    const float3 camera_pos_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 distance_ws = abs(sample_pos_ws - camera_pos_ws);
    return max(max(distance_ws.x, distance_ws.y), distance_ws.z);
}

// 全カスケードの解像度が同じ立方体で、セルサイズが段ごとに2倍になることはCPU側で検証済み。
// カスケード選択とディザ補間には、どちらもカメラの連続位置を中心とする境界を使う。
bool FspTrySelectFinestDenseIrradianceVolumeCascade(out uint cascade_index, float3 sample_pos_ws)
{
    const uint cascade_count = FspCascadeCount();
    const float distance_ws = FspDenseIrradianceVolumeCameraDistance(sample_pos_ws);
    const float finest_half_extent = FspDenseIrradianceVolumeSelectionHalfExtent(0u);
    const uint required_scale = max(1u, (uint)ceil(distance_ws / finest_half_extent));
    cascade_index = min(
        (required_scale <= 1u) ? 0u : uint(firstbithigh(required_scale - 1u) + 1),
        cascade_count - 1u);

    // 親を持たない最終カスケードでは、実グリッドの全範囲を維持する。
    return FspIsWorldPosInsideDenseIrradianceVolumeCascade(sample_pos_ws, cascade_index);
}

float FspCalcDenseIrradianceVolumeCascadeBoundaryDitherRate(float3 sample_pos_ws, uint cascade_index)
{
    if((cascade_index + 1u) >= FspCascadeCount())
    {
        return 0.0;
    }

    const float half_extent = FspDenseIrradianceVolumeSelectionHalfExtent(cascade_index);
    const float boundary_dist = half_extent - FspDenseIrradianceVolumeCameraDistance(sample_pos_ws);
    const float coarse_cell_size = FspGetCascadeParam(cascade_index + 1u).grid.cell_size;
    // 小さいグリッドでも補間帯が重ならないようにする。
    // 子から親への遷移率が1になる境界では、親からさらに上位への遷移率を0に保つ。
    const float dither_width = min(coarse_cell_size, half_extent * 0.5);
    return 1.0 - saturate(boundary_dist / max(dither_width, 1e-5));
}

// ライティング時に使うcascadeを1本だけ選ぶ。境界では親cascadeとの確率的遷移だけを行う。
bool FspTrySelectLightingCascade(out uint cascade_index, float3 sample_pos_ws, float2 dither_seed)
{
    if(!FspTrySelectFinestDenseIrradianceVolumeCascade(cascade_index, sample_pos_ws))
    {
        return false;
    }

    // 境界帯だけcoarse cascadeへ確率的に逃がし、ブレンドではなくディザで切り替える。
    const float coarse_select_rate =
        FspCalcDenseIrradianceVolumeCascadeBoundaryDitherRate(sample_pos_ws, cascade_index);
    if(coarse_select_rate > 0.0 && interleaved_gradient_noise(dither_seed) < coarse_select_rate)
    {
        cascade_index = min(cascade_index + 1u, FspCascadeCount() - 1u);
    }
    return true;
}

// Dense IrradianceVolume の nearest 参照。
bool TrySampleFspPackedShL1Nearest(out FspProbePackedShL1Sample result, float3 sample_pos_ws)
{
    result = MakeZeroFspProbePackedShL1Sample();

    uint cascade_index = 0;
    if(!FspTrySelectLightingCascade(cascade_index, sample_pos_ws, 0.0.xx))
    {
        return false;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const int3 linear_coord = clamp(
        int3(floor((sample_pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv)),
        0,
        cascade.grid.grid_resolution - 1);
    const uint irradiance_volume_cell_index =
        FspIrradianceVolumeCellIndexFromLinearCoord(cascade_index, linear_coord);
    result = FspLoadPackedShL1FromCellIndexUnchecked(irradiance_volume_cell_index);
    return true;
}

float4 FspTrilinearLoadIrradianceVolumeCoeff(
    uint4 sh_address_z0,
    uint4 sh_address_z1,
    uint coeff_index,
    float3 lerp_rate)
{
    const float4 z0_y0 = lerp(
        FspIrradianceVolumeSHBuffer[sh_address_z0.x + coeff_index],
        FspIrradianceVolumeSHBuffer[sh_address_z0.y + coeff_index],
        lerp_rate.x);
    const float4 z0_y1 = lerp(
        FspIrradianceVolumeSHBuffer[sh_address_z0.z + coeff_index],
        FspIrradianceVolumeSHBuffer[sh_address_z0.w + coeff_index],
        lerp_rate.x);
    const float4 z1_y0 = lerp(
        FspIrradianceVolumeSHBuffer[sh_address_z1.x + coeff_index],
        FspIrradianceVolumeSHBuffer[sh_address_z1.y + coeff_index],
        lerp_rate.x);
    const float4 z1_y1 = lerp(
        FspIrradianceVolumeSHBuffer[sh_address_z1.z + coeff_index],
        FspIrradianceVolumeSHBuffer[sh_address_z1.w + coeff_index],
        lerp_rate.x);
    return lerp(
        lerp(z0_y0, z0_y1, lerp_rate.y),
        lerp(z1_y0, z1_y1, lerp_rate.y),
        lerp_rate.z);
}

// Dense IrradianceVolume 前提の固定コスト Trilinear 参照。
// CPU検証済みのcubic power-of-two解像度を使い、ToroidalMappingはx/y/z各軸の2座標だけ事前計算する。
// 8セルごとのmodulo/Morton encodeは行わず、X-majorのrow/slice stride加算だけでaddressを構築する。
bool TrySampleFspPackedShL1Interpolated(out FspProbePackedShL1Sample result, float3 sample_pos_ws, float2 dither_seed)
{
    uint cascade_index = 0;
    if(!FspTrySelectLightingCascade(cascade_index, sample_pos_ws, dither_seed))
    {
        return false;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 grid_coordf = (sample_pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv - float3(0.5, 0.5, 0.5);
    const int3 base_coord = clamp(int3(floor(grid_coordf)), int3(0, 0, 0), cascade.grid.grid_resolution - 2);
    const float3 lerp_rate = saturate(grid_coordf - float3(base_coord));

    const int3 physical_coord0 =
        FspIrradianceVolumeToroidalPhysicalCoord(base_coord, cascade.grid);
    const int3 physical_coord1 =
        (physical_coord0 + 1) & (cascade.grid.grid_resolution - 1);
    const uint row_stride = uint(cascade.grid.grid_resolution.x);
    const uint slice_stride = uint(cascade.grid.grid_resolution.x * cascade.grid.grid_resolution.y);
    const uint x0 = uint(physical_coord0.x);
    const uint x1 = uint(physical_coord1.x);
    const uint y0 = uint(physical_coord0.y) * row_stride;
    const uint y1 = uint(physical_coord1.y) * row_stride;
    const uint z0 = uint(physical_coord0.z) * slice_stride;
    const uint z1 = uint(physical_coord1.z) * slice_stride;
    const uint cascade_offset = cascade.cell_offset;
    const uint4 cell_index_z0 = cascade_offset + uint4(
        x0 + y0 + z0,
        x1 + y0 + z0,
        x0 + y1 + z0,
        x1 + y1 + z0);
    const uint4 cell_index_z1 = cascade_offset + uint4(
        x0 + y0 + z1,
        x1 + y0 + z1,
        x0 + y1 + z1,
        x1 + y1 + z1);
    const uint4 sh_address_z0 = cell_index_z0 * k_fsp_irradiance_volume_sh_float4_count;
    const uint4 sh_address_z1 = cell_index_z1 * k_fsp_irradiance_volume_sh_float4_count;

    const float4 coeff0 = FspTrilinearLoadIrradianceVolumeCoeff(sh_address_z0, sh_address_z1, 0u, lerp_rate);
    const float4 coeff1 = FspTrilinearLoadIrradianceVolumeCoeff(sh_address_z0, sh_address_z1, 1u, lerp_rate);
    const float4 coeff2 = FspTrilinearLoadIrradianceVolumeCoeff(sh_address_z0, sh_address_z1, 2u, lerp_rate);
    const float4 coeff3 = FspTrilinearLoadIrradianceVolumeCoeff(sh_address_z0, sh_address_z1, 3u, lerp_rate);
    result.sky_visibility_sh = float4(coeff0.r, coeff1.r, coeff2.r, coeff3.r);
    result.radiance_sh_r = float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g);
    result.radiance_sh_g = float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b);
    result.radiance_sh_b = float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a);

    return true;
}

// FSP ライティングの入口。nearest / interpolated をここで切り替える。
bool TrySampleFspPackedShL1(out FspProbePackedShL1Sample result, float3 sample_pos_ws, float2 dither_seed)
{
    if(0 != cb_instant_rdv.fsp_lighting_interpolation_enable)
    {
        return TrySampleFspPackedShL1Interpolated(result, sample_pos_ws, dither_seed);
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
        && (cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.is_enable_radiance || cb_ngl_lighting_pass.dbg_view_instant_rdv_sky_visibility))
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

        if(cb_ngl_lighting_pass.gi_sample_mode == k_gi_sample_mode_fsp)
        {
            FspProbePackedShL1Sample fsp_probe_sh;
            if(TrySampleFspPackedShL1(fsp_probe_sh, gi_sample_pos_ws, input.pos.xy))
            {
                if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_instant_rdv_sky_visibility)
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
                if(cb_ngl_lighting_pass.is_enable_sky_visibility || cb_ngl_lighting_pass.dbg_view_instant_rdv_sky_visibility)
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
        if(cb_ngl_lighting_pass.dbg_view_instant_rdv_sky_visibility)
        {
            lit_color = diffuse_sky_visibility;
        }
    // ------------------------------------------------------------------------------

    // GBuffer emissive を最終色へ加算.
    lit_color += gb_emissive;


	return float4(lit_color, 1.0);
}

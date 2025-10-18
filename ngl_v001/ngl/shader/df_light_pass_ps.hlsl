
/*

	df_light_pass_ps.hlsl
 
 */

#include "include/brdf.hlsli"
#include "include/math_util.hlsli"

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};

#include "include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
ConstantBuffer<SceneDirectionalShadowSampleInfo> ngl_cb_shadowview;

struct CbLightingPass
{
	int enable_feedback_blur_test;
	int is_first_frame;

    int is_enable_gi;
    int dbg_view_ssvg_sky_visibility;
};
ConstantBuffer<CbLightingPass> ngl_cb_lighting_pass;

Texture2D tex_lineardepth;// Linear View Depth.
Texture2D tex_gbuffer0;
Texture2D tex_gbuffer1;
Texture2D tex_gbuffer2;
Texture2D tex_gbuffer3;
Texture2D tex_prev_light;
Texture2D tex_shadowmap;
SamplerState samp;
SamplerComparisonState samp_shadow;

TextureCube tex_ibl_diffuse;
TextureCube tex_ibl_specular;
Texture2D tex_ibl_dfg;

// GI
#include "ssvg/ssvg_util.hlsli"


// DirectionalLight評価. 標準.
void EvalDirectionalLightStandard
(
	out float3 out_diffuse, out float3 out_specular,

	float3 light_intensity, float3 L, 
	float3 N, float3 V,
	float3 base_color, float roughness, float metalness
)
{
	const float3 F = brdf_schlick_roughness_F(compute_F0_default(base_color, metalness), roughness, N, V, L);// Roughnessを考慮したFresnel
	const float3 kD = (1.0 - F);

	const float3 diffuse_term = brdf_lambert(base_color, roughness, metalness, N, V, L);
	const float3 specular_term = brdf_standard_ggx(base_color, roughness, metalness, N, V, L);
	const float3 brdf = specular_term + diffuse_term;
	const float cos_term = saturate(dot(N, L));

	//out_diffuse = (float3)0;
	out_diffuse = cos_term * diffuse_term * kD * light_intensity;
	
	//out_specular = (float3)0;
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
	const float3 kD = (1.0 - F);

	const float3 brdf_diffuse = brdf_lambert(base_color, roughness, metalness, N, V, L_Reflect);// diffuse BRDF.

	// Roughnessによる読み取りMipの計算.
	uint ibl_spec_miplevel, ibl_spec_width, ibl_spec_height, ibl_spec_mipcount;
	tex_cube_ibl_spacular.GetDimensions(ibl_spec_miplevel, ibl_spec_width, ibl_spec_height, ibl_spec_mipcount);
	const float ibl_specular_mip = (float)(ibl_spec_mipcount-1.0) * roughness;

	const float3 irradiance_specular = tex_cube_ibl_spacular.SampleLevel(samp, L_Reflect, ibl_specular_mip).rgb;
	const float4 specular_dfg = tex_2d_specular_dfg.SampleLevel(samp, float2(saturate(dot(N, V)), roughness), 0);
	const float3 irradiance_diffuse = tex_cube_ibl_diffuse.SampleLevel(samp, N, 0).rgb;

	// FresnelでDiffuseとSpecularに分配.
	out_diffuse = brdf_diffuse * kD * irradiance_diffuse;
	out_specular = irradiance_specular * (F * specular_dfg.x + specular_dfg.y);
}

float EvalDirectionalShadow(Texture2D tex_cascade_shadowmap, SamplerComparisonState comp_samp, float3 L, float3 pixel_pos_ws, float3 normal, float3 camera_pos, float3 camera_dir)
{
	// Cascade Index.
	const float view_depth = dot(pixel_pos_ws - camera_pos, camera_dir);
	int sample_cascade_index = ngl_cb_shadowview.cb_valid_cascade_count - 1;
	for(int i = 0; i < ngl_cb_shadowview.cb_valid_cascade_count ; ++i)
	{
		if(view_depth < ngl_cb_shadowview.cb_cascade_far_distance4[i/4][i%4])
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
	const int cascade_blend_count = max(2, ngl_cb_shadowview.cb_valid_cascade_count - sample_cascade_index);
	//const int cascade_blend_count = 1;// デバッグ用. ブレンドしない.

	// 近い方のCascadeの末端で, 次のCascadeとブレンドする.
	const float cascade_blend_rate =
		(1 < cascade_blend_count)? 1.0 - saturate((ngl_cb_shadowview.cb_cascade_far_distance4[sample_cascade_index/4][sample_cascade_index%4] - view_depth) / k_cascade_blend_range_ws) : 0.0;
	
	// ブレンド対象のCascade2つに関するLitVisibility. デフォルトは影なし(1.0).
	float2 cascade_blend_lit_sample = float2(1.0, 1.0);
	for(int cascade_i = 0; cascade_i < cascade_blend_count; ++cascade_i)
	{
		const int cascade_index = sample_cascade_index + cascade_i;
		const float cascade_size_rate_based_on_c0 = (ngl_cb_shadowview.cb_cascade_far_distance4[cascade_index/4][cascade_index%4]) / ngl_cb_shadowview.cb_cascade_far_distance4[0][0];
	
		const float slope_rate = (1.0 - saturate(dot(L, normal)));
		const float slope_bias_ws = k_coef_slope_bias_ws * slope_rate;
		const float normal_bias_ws = k_coef_normal_bias_ws * slope_rate;
		float shadow_sample_bias = max(k_coef_constant_bias_ws, slope_bias_ws);

		float3 shadow_sample_bias_vec_ws = (L * shadow_sample_bias) + (normal * normal_bias_ws);
		shadow_sample_bias_vec_ws *= cascade_size_rate_based_on_c0;// Cascadeのサイズによる補正項.
	
		const float3 pixel_pos_shadow_vs = mul(ngl_cb_shadowview.cb_shadow_view_mtx[cascade_index], float4(pixel_pos_ws + shadow_sample_bias_vec_ws, 1.0));
		const float4 pixel_pos_shadow_cs = mul(ngl_cb_shadowview.cb_shadow_proj_mtx[cascade_index], float4(pixel_pos_shadow_vs, 1.0));
		const float3 pixel_pos_shadow_cs_pd = pixel_pos_shadow_cs.xyz;;
		
		const float2 cascade_uv_lt = ngl_cb_shadowview.cb_cascade_tile_uvoffset_uvscale[cascade_index].xy;
		const float2 cascade_uv_size = ngl_cb_shadowview.cb_cascade_tile_uvoffset_uvscale[cascade_index].zw;
	
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
    // 境界部分の +1.
    return calc_2d_position_from_index(index, tex_width) * k_probe_octmap_width_with_border + 1.0;
}

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{	
	// リニアView深度.
	const float ld = tex_lineardepth.Load(int3(input.pos.xy, 0)).r;// LightingBufferとGBufferが同じ解像度前提でLoad.
	if(1e7 <= ld)
	{
		// 天球扱い.
		//return float4(0.0, 0.0, 0.5, 0.0);
		discard;
	}
	float depth_visualize = pow(saturate(ld / 200.0), 1.0/0.8);
	
	// LightingBufferとGBufferが同じ解像度前提でLoad.
	const float4 gb0 = tex_gbuffer0.Load(int3(input.pos.xy, 0));
	const float4 gb1 = tex_gbuffer1.Load(int3(input.pos.xy, 0));
	const float4 gb2 = tex_gbuffer2.Load(int3(input.pos.xy, 0));
	const float4 gb3 = tex_gbuffer3.Load(int3(input.pos.xy, 0));
	const float4 prev_light = tex_prev_light.Load(int3(input.pos.xy, 0));

	// GBuffer Decode.
	float3 gb_base_color = gb0.xyz;
	float gb_occlusion = gb0.w;
	float3 gb_normal_ws = normalize(gb1.xyz * 2.0 - 1.0);// gbufferからWorldNormalデコード.
	float gb_roughness = gb2.x;
	float gb_metalness = gb2.y;
	float gb_surface_option = gb2.z;
	float gb_material_id = gb2.w;
	float3 gb_emissive = gb3.xyz;

	
	const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

	// ピクセルへのワールド空間レイを計算.
	const float3 to_pixel_ray_vs = CalcViewSpaceRay(input.uv, ngl_cb_sceneview.cb_proj_mtx);
	const float3 pixel_pos_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * ld, 1.0));
	const float3 to_pixel_vec_ws = pixel_pos_ws - camera_pos;
	const float3 to_pixel_ray_ws = normalize(to_pixel_vec_ws);

	
	const float3 lit_intensity = float3(1.0, 1.0, 1.0) * NGL_PI * 1.5;
	const float3 lit_dir = normalize(ngl_cb_shadowview.cb_shadow_view_inv_mtx[0]._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.


	const float3 V = -to_pixel_ray_ws;
	const float3 L = -lit_dir;
	
	// Directional Shadow.
	float light_visibility = EvalDirectionalShadow(tex_shadowmap, samp_shadow, L, pixel_pos_ws, gb_normal_ws, camera_pos, camera_dir);

	float3 lit_color = (float3)0;

	// Directional Lit.
	{
		float3 dlit_diffuse, dlit_specular;
		EvalDirectionalLightStandard(dlit_diffuse, dlit_specular, lit_intensity, L, gb_normal_ws, V, gb_base_color, gb_roughness, gb_metalness);

		lit_color += (dlit_diffuse + dlit_specular) * light_visibility;
		
		//lit_color += (dlit_diffuse) * light_visibility;// テスト.
		//lit_color += (dlit_specular) * light_visibility;// テスト.
	}

    // GI.
    float3 sky_visibility = 1.0;;
    if(ngl_cb_lighting_pass.is_enable_gi)
    {
        uint tex_width, tex_height;
        TexProbeSkyVisibility.GetDimensions(tex_width, tex_height);
        const float2 texel_size = 1.0 / float2(tex_width, tex_height);

        const float2 octmap_local_texel_pos = OctEncode(gb_normal_ws)*k_probe_octmap_width;


        // Probeサンプルする位置を補正するなど.
        const float normal_bias = 0.1;//0.25;
        const float camera_bias = 0.1;//0.1;
        const float3 sample_pos_ws = pixel_pos_ws + gb_normal_ws * (normal_bias * cb_ssvg.cell_size) + V * (camera_bias * cb_ssvg.cell_size);

        const float3 voxel_coordf = (sample_pos_ws - cb_ssvg.grid_min_pos) * cb_ssvg.cell_size_inv;

        #if 1
            // 8近傍補間.
            const int3 voxel_base_coord = floor(voxel_coordf - 0.5);
            const float3 coord_frac = frac(voxel_coordf - 0.5);
            const uint voxel_index_000 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_001 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(1, 0, 0), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_010 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(0, 1, 0), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_011 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(1, 1, 0), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_100 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(0, 0, 1), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_101 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(1, 0, 1), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_110 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(0, 1, 1), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
            const uint voxel_index_111 = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + int3(1, 1, 1), cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);

            const float2 octmap_uv_000 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_000, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_001 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_001, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_010 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_010, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_011 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_011, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_100 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_100, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_101 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_101, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_110 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_110, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
            const float2 octmap_uv_111 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_111, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;

            #if 0
                // 単純トリリニア補間.
                const float svx0 = lerp(TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_000, 0).r, TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_001, 0).r, coord_frac.x);
                const float svx1 = lerp(TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_010, 0).r, TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_011, 0).r, coord_frac.x);
                const float svx2 = lerp(TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_100, 0).r, TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_101, 0).r, coord_frac.x);
                const float svx3 = lerp(TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_110, 0).r, TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_111, 0).r, coord_frac.x);

                const float svy0 = lerp(svx0, svx1, coord_frac.y);
                const float svy1 = lerp(svx2, svx3, coord_frac.y);

                const float sv = lerp(svy0, svy1, coord_frac.z);
            #elif 1
                // サンプル位置の可視性チェック検証

                const float3 to_sample_vec_000 = -(float3(0,0,0)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_001 = -(float3(1,0,0)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_010 = -(float3(0,1,0)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_011 = -(float3(1,1,0)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_100 = -(float3(0,0,1)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_101 = -(float3(1,0,1)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_110 = -(float3(0,1,1)-coord_frac) * cb_ssvg.cell_size;
                const float3 to_sample_vec_111 = -(float3(1,1,1)-coord_frac) * cb_ssvg.cell_size;

                const float3 to_sample_dir_000 = normalize(to_sample_vec_000);
                const float3 to_sample_dir_001 = normalize(to_sample_vec_001);
                const float3 to_sample_dir_010 = normalize(to_sample_vec_010);
                const float3 to_sample_dir_011 = normalize(to_sample_vec_011);
                const float3 to_sample_dir_100 = normalize(to_sample_vec_100);
                const float3 to_sample_dir_101 = normalize(to_sample_vec_101);
                const float3 to_sample_dir_110 = normalize(to_sample_vec_110);
                const float3 to_sample_dir_111 = normalize(to_sample_vec_111);

                const float to_sample_len_000 = length(to_sample_vec_000);
                const float to_sample_len_001 = length(to_sample_vec_001);
                const float to_sample_len_010 = length(to_sample_vec_010);
                const float to_sample_len_011 = length(to_sample_vec_011);
                const float to_sample_len_100 = length(to_sample_vec_100);
                const float to_sample_len_101 = length(to_sample_vec_101);
                const float to_sample_len_110 = length(to_sample_vec_110);
                const float to_sample_len_111 = length(to_sample_vec_111);

                const float2 to_sample_octmap_uv_000 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_000, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_000)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_001 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_001, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_001)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_010 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_010, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_010)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_011 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_011, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_011)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_100 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_100, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_100)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_101 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_101, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_101)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_110 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_110, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_110)*k_probe_octmap_width) * texel_size;
                const float2 to_sample_octmap_uv_111 =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index_111, cb_ssvg.probe_atlas_texture_base_width)) + OctEncode(to_sample_dir_111)*k_probe_octmap_width) * texel_size;

                const float to_sample_dist_000 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_000, 0).r;
                const float to_sample_dist_001 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_001, 0).r;
                const float to_sample_dist_010 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_010, 0).r;
                const float to_sample_dist_011 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_011, 0).r;
                const float to_sample_dist_100 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_100, 0).r;
                const float to_sample_dist_101 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_101, 0).r;
                const float to_sample_dist_110 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_110, 0).r;
                const float to_sample_dist_111 = TexProbeSkyVisibility.SampleLevel(samp, to_sample_octmap_uv_111, 0).r;

                #if 1
                    /*
                    const float to_sample_visibility_000 = (to_sample_dist_000 < to_sample_len_000)? 0.01 : 1.0;
                    const float to_sample_visibility_001 = (to_sample_dist_001 < to_sample_len_001)? 0.01 : 1.0;
                    const float to_sample_visibility_010 = (to_sample_dist_010 < to_sample_len_010)? 0.01 : 1.0;
                    const float to_sample_visibility_011 = (to_sample_dist_011 < to_sample_len_011)? 0.01 : 1.0;
                    const float to_sample_visibility_100 = (to_sample_dist_100 < to_sample_len_100)? 0.01 : 1.0;
                    const float to_sample_visibility_101 = (to_sample_dist_101 < to_sample_len_101)? 0.01 : 1.0;
                    const float to_sample_visibility_110 = (to_sample_dist_110 < to_sample_len_110)? 0.01 : 1.0;
                    const float to_sample_visibility_111 = (to_sample_dist_111 < to_sample_len_111)? 0.01 : 1.0;
                    */
                    const float to_sample_visibility_000 = max(0.01, to_sample_dist_000 - to_sample_len_000);
                    const float to_sample_visibility_001 = max(0.01, to_sample_dist_001 - to_sample_len_001);
                    const float to_sample_visibility_010 = max(0.01, to_sample_dist_010 - to_sample_len_010);
                    const float to_sample_visibility_011 = max(0.01, to_sample_dist_011 - to_sample_len_011);
                    const float to_sample_visibility_100 = max(0.01, to_sample_dist_100 - to_sample_len_100);
                    const float to_sample_visibility_101 = max(0.01, to_sample_dist_101 - to_sample_len_101);
                    const float to_sample_visibility_110 = max(0.01, to_sample_dist_110 - to_sample_len_110);
                    const float to_sample_visibility_111 = max(0.01, to_sample_dist_111 - to_sample_len_111);

                #else
                    const float to_sample_visibility_000 = to_sample_dist_000 / (to_sample_len_000 + 0.01);
                    const float to_sample_visibility_001 = to_sample_dist_001 / (to_sample_len_001 + 0.01);
                    const float to_sample_visibility_010 = to_sample_dist_010 / (to_sample_len_010 + 0.01);
                    const float to_sample_visibility_011 = to_sample_dist_011 / (to_sample_len_011 + 0.01);
                    const float to_sample_visibility_100 = to_sample_dist_100 / (to_sample_len_100 + 0.01);
                    const float to_sample_visibility_101 = to_sample_dist_101 / (to_sample_len_101 + 0.01);
                    const float to_sample_visibility_110 = to_sample_dist_110 / (to_sample_len_110 + 0.01);
                    const float to_sample_visibility_111 = to_sample_dist_111 / (to_sample_len_111 + 0.01);
                #endif

                #if 1
                    const float w_000 = to_sample_visibility_000;
                    const float w_001 = to_sample_visibility_001;
                    const float w_010 = to_sample_visibility_010;
                    const float w_011 = to_sample_visibility_011;
                    const float w_100 = to_sample_visibility_100;
                    const float w_101 = to_sample_visibility_101;
                    const float w_110 = to_sample_visibility_110;
                    const float w_111 = to_sample_visibility_111;
                #else
                    const float w_000 = to_sample_dist_000;
                    const float w_001 = to_sample_dist_001;
                    const float w_010 = to_sample_dist_010;
                    const float w_011 = to_sample_dist_011;
                    const float w_100 = to_sample_dist_100;
                    const float w_101 = to_sample_dist_101;
                    const float w_110 = to_sample_dist_110;
                    const float w_111 = to_sample_dist_111;
                #endif

                
                #if 1
                    float sv = 
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_000, 0).r * w_000 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_001, 0).r * w_001 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_010, 0).r * w_010 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_011, 0).r * w_011 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_100, 0).r * w_100 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_101, 0).r * w_101 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_110, 0).r * w_110 +
                        TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_111, 0).r * w_111;
                #else
                    float3 sv = 
                        to_sample_dir_000 * w_000 +
                        to_sample_dir_001 * w_001 +
                        to_sample_dir_010 * w_010 +
                        to_sample_dir_011 * w_011 +
                        to_sample_dir_100 * w_100 +
                        to_sample_dir_101 * w_101 +
                        to_sample_dir_110 * w_110 +
                        to_sample_dir_111 * w_111;
                #endif
                sv /= (w_000 + w_001 + w_010 + w_011 + w_100 + w_101 + w_110 + w_111);
                sv /= 20.0;// 適当に正規化.

            #else
                // 方向考慮の重み付け.
                const float3 position_weight = 1.0 - coord_frac;
                const float w000 = saturate(dot(float3(0,0,0)-coord_frac, gb_normal_ws)) * position_weight.x        * position_weight.y * position_weight.z;
                const float w001 = saturate(dot(float3(1,0,0)-coord_frac, gb_normal_ws)) * (1.0 -position_weight.x) * position_weight.y * position_weight.z;
                const float w010 = saturate(dot(float3(0,1,0)-coord_frac, gb_normal_ws)) * position_weight.x        * (1.0 -position_weight.y) * position_weight.z;
                const float w011 = saturate(dot(float3(1,1,0)-coord_frac, gb_normal_ws)) * (1.0 -position_weight.x) * (1.0 -position_weight.y) * position_weight.z;
                const float w100 = saturate(dot(float3(0,0,1)-coord_frac, gb_normal_ws)) * position_weight.x        * position_weight.y * (1.0 -position_weight.z);
                const float w101 = saturate(dot(float3(1,0,1)-coord_frac, gb_normal_ws)) * (1.0 -position_weight.x) * position_weight.y * (1.0 -position_weight.z);
                const float w110 = saturate(dot(float3(0,1,1)-coord_frac, gb_normal_ws)) * position_weight.x        * (1.0 -position_weight.y) * (1.0 -position_weight.z);
                const float w111 = saturate(dot(float3(1,1,1)-coord_frac, gb_normal_ws)) * (1.0 -position_weight.x) * (1.0 -position_weight.y) * (1.0 -position_weight.z);
                float sv = 
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_000, 0).r * w000 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_001, 0).r * w001 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_010, 0).r * w010 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_011, 0).r * w011 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_100, 0).r * w100 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_101, 0).r * w101 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_110, 0).r * w110 +
                    TexProbeSkyVisibility.SampleLevel(samp, octmap_uv_111, 0).r * w111;
                sv /= (w000 + w001 + w010 + w011 + w100 + w101 + w110 + w111);
            #endif
        #else
            // 最近傍サンプリング.
            float sv = 1.0;
            
            const int3 voxel_coord = floor(voxel_coordf);
            if(all(voxel_coord >= 0) && all(voxel_coord < cb_ssvg.base_grid_resolution))
            {
                // 範囲内.
                const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution), cb_ssvg.base_grid_resolution);
                const float2 octmap_uv =  (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_index, cb_ssvg.probe_atlas_texture_base_width)) + octmap_local_texel_pos) * texel_size;
                sv = TexProbeSkyVisibility.SampleLevel(samp, octmap_uv, 0).r;
            }
        #endif

        sky_visibility = sv;
    }
	
	// IBL.
	{
		float3 ibl_diffuse, ibl_specular;
		EvalIblDiffuseStandard(ibl_diffuse, ibl_specular, tex_ibl_diffuse, tex_ibl_specular, tex_ibl_dfg, samp, gb_normal_ws, V, gb_base_color, gb_roughness, gb_metalness);

		lit_color += (ibl_diffuse + ibl_specular) * sky_visibility;
		
		//lit_color = ibl_diffuse + ibl_specular;// テスト
		//lit_color = ibl_diffuse;// テスト.
		//lit_color = ibl_specular;// テスト.
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
	if(ngl_cb_lighting_pass.enable_feedback_blur_test)
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
        // sky_visibilityテスト.
        if(ngl_cb_lighting_pass.dbg_view_ssvg_sky_visibility)
        {
            lit_color = sky_visibility;
        }
    // ------------------------------------------------------------------------------


	return float4(lit_color, 1.0);
}

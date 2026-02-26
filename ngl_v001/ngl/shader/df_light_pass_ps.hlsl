
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
ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
ConstantBuffer<SceneDirectionalShadowSampleInfo> cb_ngl_shadowview;

struct CbLightingPass
{
	int enable_feedback_blur_test;
	int is_first_frame;

    float d_lit_intensity;
    float sky_lit_intensity;

    int is_enable_gi;
    float probe_sample_offset_view;
    float probe_sample_offset_surface_normal;
    float probe_sample_offset_bent_normal;
    int dbg_view_ssvg_sky_visibility;
};
ConstantBuffer<CbLightingPass> cb_ngl_lighting_pass;

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
    // 境界部分の +1.
    return calc_2d_position_from_index(index, tex_width) * k_probe_octmap_width_with_border + 1.0;
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

    // GIのテスト(Dynamic Sky Visibility).
    float sky_visibility = 1.0;
    if(cb_ngl_lighting_pass.is_enable_gi)
    {
        #if 1
            // ScreenSpaceProbeテスト.
            uint2 ss_probe_tex_size;
            ScreenSpaceProbeTex.GetDimensions(ss_probe_tex_size.x, ss_probe_tex_size.y);
            const float2 ss_probe_texel_uv_size = 1.0 / float2(ss_probe_tex_size);

            // Spherical Octahedral Map.
			const float2 ss_probe_tex_size_f = float2(ss_probe_tex_size);
			const int2 ss_probe_tile_id = int2(floor(screen_uv * ss_probe_tex_size_f / SCREEN_SPACE_PROBE_TILE_SIZE));
			const int2 ss_probe_tile_base_pos = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;
			const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id, 0));
			const float3 ss_probe_tile_normal_ws = OctDecode(ss_probe_tile_info.zw);
            // Octmapのテクセルが外側をBilinearSamplingしないようにクランプ.
			float2 octmap_texel_pos = clamp(OctEncodeHemiByNormal(gb_normal_ws, ss_probe_tile_normal_ws) * SCREEN_SPACE_PROBE_TILE_SIZE, 0.5, SCREEN_SPACE_PROBE_TILE_SIZE-0.5);
            float2 ss_probe_sample_texel_pos = ss_probe_tile_base_pos + octmap_texel_pos;
            float2 ss_probe_sample_uv = ss_probe_sample_texel_pos * ss_probe_texel_uv_size;

            float4 ss_probe_value = ScreenSpaceProbeTex.SampleLevel(samp, ss_probe_sample_uv, 0);

            sky_visibility = saturate(ss_probe_value.r);
        #else
            uint tex_width, tex_height;
            WcpProbeAtlasTex.GetDimensions(tex_width, tex_height);
            const float2 texel_size = 1.0 / float2(tex_width, tex_height);

            const float2 octmap_local_texel_pos = OctEncode(gb_normal_ws)*k_probe_octmap_width;
            float3 probe_sample_pos_ws = pixel_pos_ws;
            #if 1
                // BentNormalでサンプル位置をオフセットすることでライトリーク緩和の検証.
                {
                    // View Offset.
                    probe_sample_pos_ws += V * cb_ngl_lighting_pass.probe_sample_offset_view;

                    // Surface Normal Offset.
                    probe_sample_pos_ws += gb_normal_ws * cb_ngl_lighting_pass.probe_sample_offset_surface_normal;

                    // BentNormal Offset.
                    probe_sample_pos_ws += bent_normal_sample.xyz * cb_ngl_lighting_pass.probe_sample_offset_bent_normal;
                }
            #endif

            const float3 voxel_coordf = (probe_sample_pos_ws - cb_ssvg.wcp.grid_min_pos) * cb_ssvg.wcp.cell_size_inv;
            const int3 voxel_base_coord = floor(voxel_coordf - 0.5);
            const float3 coord_frac = frac(voxel_coordf - 0.5);

                const int3 vtx_pos[8] = {
                    int3(0, 0, 0),int3(1, 0, 0),
                    int3(0, 1, 0),int3(1, 1, 0),
                    int3(0, 0, 1),int3(1, 0, 1),
                    int3(0, 1, 1),int3(1, 1, 1)
                };

                uint voxel_indexs[8];
                float2 octmap_uvs[8];
                for (int i = 0; i < 8; ++i)
                {
                    voxel_indexs[i] = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_base_coord + vtx_pos[i], cb_ssvg.wcp.grid_toroidal_offset, cb_ssvg.wcp.grid_resolution), cb_ssvg.wcp.grid_resolution);
                    octmap_uvs[i] = (float2(calc_probe_octahedral_map_atlas_texel_base_pos(voxel_indexs[i], cb_ssvg.wcp.flatten_2d_width)) + octmap_local_texel_pos) * texel_size;
                }

                // k_wcp_probe_distance_max で正規化された [0,1] のDistanceProbe.
                float4 dir_d[2];
                for (int i = 0; i < 2; ++i)
                {
                    dir_d[i].x = WcpProbeAtlasTex.SampleLevel(samp, octmap_uvs[i + 0], 0).r;
                    dir_d[i].y = WcpProbeAtlasTex.SampleLevel(samp, octmap_uvs[i + 2], 0).r;
                    dir_d[i].z = WcpProbeAtlasTex.SampleLevel(samp, octmap_uvs[i + 4], 0).r;
                    dir_d[i].w = WcpProbeAtlasTex.SampleLevel(samp, octmap_uvs[i + 6], 0).r;
                }
                
                const float4 lerp_d_x = lerp(dir_d[0], dir_d[1], coord_frac.x);// x補間
                const float2 lerp_d_y = lerp(lerp_d_x.xz, lerp_d_x.yw, coord_frac.y);// y補間
                const float lerp_d_z = lerp(lerp_d_y.x, lerp_d_y.y, coord_frac.z);// z補間
                
            sky_visibility = lerp_d_z;
        #endif
    }
	
	// IBL.
	{
		float3 ibl_diffuse, ibl_specular;
		EvalIblDiffuseStandard(ibl_diffuse, ibl_specular, tex_ibl_diffuse, tex_ibl_specular, tex_ibl_dfg, samp, gb_normal_ws, V, gb_base_color, gb_roughness, gb_metalness);

		lit_color += (ibl_diffuse + ibl_specular) * cb_ngl_lighting_pass.sky_lit_intensity * sky_visibility * ssao_sample.a;
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
        // sky_visibilityテスト.
        if(cb_ngl_lighting_pass.dbg_view_ssvg_sky_visibility)
        {
            lit_color = sky_visibility;
        }
    // ------------------------------------------------------------------------------


	return float4(lit_color, 1.0);
}

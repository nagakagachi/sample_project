
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
	float3 gb_normal_ws = gb1.xyz * 2.0 - 1.0;// gbufferからWorldNormalデコード.
	float gb_rounghness = gb2.x;
	float gb_metalness = gb2.y;
	float gb_surface_option = gb2.z;
	float gb_material_id = gb2.w;
	float3 gb_emissive = gb3.xyz;

	
	const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.

	// ピクセルへのワールド空間レイを計算.
	const float3 to_pixel_ray_vs = CalcViewSpaceRay(input.uv, ngl_cb_sceneview.cb_proj_mtx);
	const float3 pixel_pos_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * ld, 1.0));
	const float3 to_pixel_vec_ws = pixel_pos_ws - ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;
	const float3 to_pixel_ray_ws = normalize(to_pixel_vec_ws);

	
	const float3 lit_intensity = float3(1.0, 1.0, 1.0) * ngl_PI * 1.5;
	const float3 lit_dir = normalize(ngl_cb_shadowview.cb_shadow_view_inv_mtx[0]._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.


	const float3 V = -to_pixel_ray_ws;
	const float3 L = -lit_dir;
	
	float light_visibility = 1.0;
	int sample_cascade_index_debug = 0;
	{
		// Cascade Index.
		const float view_depth = dot(to_pixel_vec_ws, camera_dir);
		int sample_cascade_index = ngl_cb_shadowview.cb_valid_cascade_count - 1;
		for(int i = 0; i < ngl_cb_shadowview.cb_valid_cascade_count ; ++i)
		{
			if(view_depth < ngl_cb_shadowview.cb_cascade_far_distance4[i/4][i%4])
			{
				sample_cascade_index = i;
				break;
			}
		}
		sample_cascade_index_debug = sample_cascade_index;

		
		// Shadowmap Sample.
		const float k_coef_constant_bias_ws = 0.01;
		const float k_coef_slope_bias_ws = 0.02;
		const float k_coef_normal_bias_ws = 0.03;
		
		const float k_cascade_blend_range_ws = 5.0;
		
		// 補間用の次のCascade.
		const int cascade_blend_count = max(2, ngl_cb_shadowview.cb_valid_cascade_count - sample_cascade_index);
		//const int cascade_blend_count = 1;// デバッグ用にブレンドしない.

		// 近い方のCascadeの末端で, 次のCascadeとブレンドする.
		const float cascade_blend_rate =
			(1 < cascade_blend_count)? 1.0 - saturate((ngl_cb_shadowview.cb_cascade_far_distance4[sample_cascade_index/4][sample_cascade_index%4] - view_depth) / k_cascade_blend_range_ws) : 0.0;
		
		// ブレンド対象のCascade2つに関するLitVisibility. デフォルトは影なし(1.0).
		float2 cascade_blend_lit_sample = float2(1.0, 1.0);
		for(int cascade_i = 0; cascade_i < cascade_blend_count; ++cascade_i)
		{
			const int cascade_index = sample_cascade_index + cascade_i;
			const float cascade_size_rate_based_on_c0 = (ngl_cb_shadowview.cb_cascade_far_distance4[cascade_index/4][cascade_index%4]) / ngl_cb_shadowview.cb_cascade_far_distance4[0][0];
		
			const float slope_rate = (1.0 - saturate(dot(L, gb_normal_ws)));
			const float slope_bias_ws = k_coef_slope_bias_ws * slope_rate;
			const float normal_bias_ws = k_coef_normal_bias_ws * slope_rate;
			float shadow_sample_bias = max(k_coef_constant_bias_ws, slope_bias_ws);

			float3 shadow_sample_bias_vec_ws = (L * shadow_sample_bias) + (gb_normal_ws * normal_bias_ws);
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
						shadow_comp_accum += tex_shadowmap.SampleCmpLevelZero(samp_shadow, pixel_pos_shadow_uv, pixel_pos_shadow_cs_pd.z, int2(ox, oy)).x;
			
				shadow_comp_accum = shadow_comp_accum / float(k_pcf_normalizer);
#else
				shadow_comp_accum = tex_shadowmap.SampleCmpLevelZero(samp_shadow, pixel_pos_shadow_uv, pixel_pos_shadow_cs_pd.z, int2(0, 0)).x;
#endif
				
				cascade_blend_lit_sample[cascade_i] = shadow_comp_accum;
			}
		}
		light_visibility = lerp(cascade_blend_lit_sample[0], cascade_blend_lit_sample[1], cascade_blend_rate);
	}
	
	const float3 specular_term = brdf_standard_ggx(gb_base_color, gb_rounghness, gb_metalness, gb_normal_ws, V, L);
	const float3 diffuse_term = brdf_lambert(gb_base_color, gb_rounghness, gb_metalness, gb_normal_ws, V, L);
	const float3 brdf = specular_term + diffuse_term;
	const float cos_term = saturate(dot(gb_normal_ws, L));
	float3 lit_color = cos_term * brdf * lit_intensity * light_visibility;
	
		// ------------------------------------------------------------------------------
			#if 0
				// 初回フレームの情報を画面に継続保持して調査するロジック.
				if(0.1 > input.uv.y)
				{
					if(ngl_cb_lighting_pass.is_first_frame == 1)
					{
						// 初期フレーム特有の問題調査のため, 初期フレームの各種情報を出力する.
						if(0.1 > input.uv.x)
						{
							return float4(diffuse_term, 0.0);
						}
						if(0.2 > input.uv.x)
						{
							return float4(specular_term, 0.0);
						}
						if(0.3 > input.uv.x)
						{
							return float4(gb_base_color, 0.0);
						}
						if(0.4 > input.uv.x)
						{
							return float4(gb_rounghness.xxx, 0.0);
						}
						if(0.5 > input.uv.x)
						{
							return float4(gb_metalness.xxx, 0.0);
						}
						if(0.6 > input.uv.x)
						{
							return float4(gb_normal_ws, 0.0);
						}
					}
					else
					{
						// 初期フレーム以降でも初期フレームデータをそのまま画面に維持する.
						return prev_light;
					}
				}
			#endif
			// NaNチェック.
			const float3  k_lit_nan_key_color = float3(1.0, 0.25, 1.0);
			if(isnan(lit_color.x) || isnan(lit_color.y) || isnan(lit_color.z))
			{
				// ライティング計算でNaN検出した場合はキーになるカラーをそのまま返す.
				return float4(k_lit_nan_key_color, 0.0);
			}
			// NaNチェック. 前回フレームのバッファがNaNキー色の場合は, そのまま返す.
			if(all(prev_light == k_lit_nan_key_color))
			{
				return float4(k_lit_nan_key_color, 0.0);
			}
		// ------------------------------------------------------------------------------

	// ambient term.
	{
		const float3 k_ambient_rate = float3(0.7, 0.7, 1.0) * 0.15;
		lit_color += gb_base_color * (1.0/ngl_PI) * ((dot(gb_normal_ws, -L)) * 0.5 + 0.5) * lit_intensity * k_ambient_rate;
	}

	// 過去フレームを使ったフィードバックブラーテスト.
	if(ngl_cb_lighting_pass.enable_feedback_blur_test)
	{
		// 画面端でテスト用のフィードバックブラー.
		const float2 dist_from_center = (input.uv - 0.5);
		const float length_from_center = length(dist_from_center);
		const float k_lenght_min = 0.4;
		float prev_blend_rate = saturate((length_from_center - k_lenght_min)*5.0);
		lit_color = lerp(lit_color, prev_light, prev_blend_rate * 0.95);
	}

	return float4(lit_color, 1.0);
}

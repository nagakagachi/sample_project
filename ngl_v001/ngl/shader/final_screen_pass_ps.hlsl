
#if 0

	最終画面描画パスPS.
		sRGB OETF も兼ねている.
#endif

#include "include/tonemap.hlsli"

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};


struct CbFinalScreenPass
{
	int enable_halfdot_gray;
	int enable_subview_result;
	int enable_raytrace_result;
	int enable_gbuffer;
	int enable_dshadow;

	int enable_general_debug;
    float general_debug_rate;
};
ConstantBuffer<CbFinalScreenPass> cb_final_screen_pass;

Texture2D tex_light;

Texture2D tex_rt;// テクスチャデバッグ.
Texture2D tex_gbuffer0;// テクスチャデバッグ.
Texture2D tex_gbuffer1;// テクスチャデバッグ.
Texture2D tex_gbuffer2;// テクスチャデバッグ.
Texture2D tex_gbuffer3;// テクスチャデバッグ.
Texture2D tex_dshadow;// テクスチャデバッグ.
Texture2D tex_res_data;// テクスチャデバッグ.
Texture2D tex_general_debug;// テクスチャデバッグ.

SamplerState samp;


float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
	// Lighting Buffer.
	float4 color = tex_light.SampleLevel(samp, input.uv, 0);

	// Tonemap Test.
	{
#if 1
		// Aces Approx.
		color.rgb = Tonemap_AcesFilm_Approx(color.rgb);
#elif 1
		// Uncharted2.
		color.rgb = Tonemap_Uncharted2_Filmic(color.rgb);
#else
		// Clamp.
		color.rgb = Tonempa_Clamp(color.rgb);
#endif
	}

	
	// Debug View.
	if(true)
	{
		if(cb_final_screen_pass.enable_raytrace_result)
		{
			const float2 debug_area_size = float2(0.5, 0.5);
			const float2 debug_area_lt = float2(0.0, 0.5);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_rt.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}

		if(cb_final_screen_pass.enable_subview_result)
		{
			const float2 debug_area_size = float2(0.3, 0.3);
			const float2 debug_area_lt = float2(0.7, 0.7);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_res_data.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}
		
		if(cb_final_screen_pass.enable_halfdot_gray)
		{
			// 50% Gray dot. 半分のピクセルが1, 半分のピクセルが0のドットで50%の輝度表現.
			const float2 debug_area_size = float2(0.1, 0.1);
			const float2 debug_area_lt = float2(0.5, 0.5);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				const float v = (int(input.pos.x) & 0x01) == (1-(int(input.pos.y) & 0x01))? 1.0 : 0.0;
				color.rgb = v.xxx;
			}
		}

		const float k_gbuffer_debug_height = 0.22;
		if(cb_final_screen_pass.enable_gbuffer)
		{
			const float2 debug_area_size = float2(k_gbuffer_debug_height, k_gbuffer_debug_height);
			const float2 debug_area_lt = float2(0.0, 0.0);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_gbuffer0.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}
		if(cb_final_screen_pass.enable_gbuffer)
		{
			const float2 debug_area_size = float2(k_gbuffer_debug_height, k_gbuffer_debug_height);
			const float2 debug_area_lt = float2(0.0, k_gbuffer_debug_height);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_gbuffer1.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}
		if(cb_final_screen_pass.enable_gbuffer)
		{
			const float2 debug_area_size = float2(k_gbuffer_debug_height, k_gbuffer_debug_height);
			const float2 debug_area_lt = float2(k_gbuffer_debug_height, 0.0);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_gbuffer2.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}
		if(cb_final_screen_pass.enable_gbuffer)
		{
			const float2 debug_area_size = float2(k_gbuffer_debug_height, k_gbuffer_debug_height);
			const float2 debug_area_lt = float2(k_gbuffer_debug_height, k_gbuffer_debug_height);
			const float2 debug_area_br = debug_area_lt + debug_area_size;
			if (all(debug_area_lt <= input.uv) && all(debug_area_br >= input.uv))
			{
				float4 sample_color = tex_gbuffer3.SampleLevel(samp, (input.uv-debug_area_lt) / debug_area_size, 0);
				color = sample_color;
			}
		}
		if(cb_final_screen_pass.enable_dshadow)
		{
			const float2 k_lt = float2(k_gbuffer_debug_height*2.0 + 0.01, 0.0);
			const float2 k_size = float2(0.25, 0.25) * float2(1, 16.0/9.0);// アスペクト比キャンセルして正方形アトラスをそのまま描画.
			const float2 area_rate = (input.uv - k_lt)/(k_size);
			if(all(0.0 < area_rate) && all(1.0 > area_rate))
			{
				float sample_shadow = tex_dshadow.SampleLevel(samp, area_rate, 0).x;
				sample_shadow = sample_shadow*sample_shadow*sample_shadow;
				return sample_shadow*2.0;
			}
		}
        // 全画面にスライダでデバッグテクスチャ表示する汎用機能. レンダラCpp側でtex_general_debugに設定するものを切り替える.
		if(0 <= cb_final_screen_pass.enable_general_debug)
		{
            // スライダで画面全体を可視化に切り替え.
            const float slider_fill_step = (input.uv.x <= cb_final_screen_pass.general_debug_rate)? 1.0 : 0.0;

			float4 general_debug_vis = float4(0,0,0,1);
            // チャンネル切り替え.
            if(0 == cb_final_screen_pass.enable_general_debug)
            {
                general_debug_vis = tex_general_debug.SampleLevel(samp, input.uv, 0);
            }
            else if(1 == cb_final_screen_pass.enable_general_debug)
            {
                general_debug_vis = tex_general_debug.SampleLevel(samp, input.uv, 0).aaaa;
            }
            
            color.rgb = lerp(color.rgb, general_debug_vis.rgb, slider_fill_step);
		}
		
	}
	
	// sRGB向けOETF.
	if(true)
	{
		color.rgb = pow(color.rgb, 1.0/2.2);
	}
	return color;
}


/*

	skybox_pass_ps.hlsl
 
 */

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};

#include "include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

Texture2D tex_skybox_panorama;
SamplerState samp;

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
#if 1
	return tex_skybox_panorama.SampleLevel(samp, input.uv, 0);
#else
	return float4(1.0, 0.0, 0.0, 1.0);
#endif
}

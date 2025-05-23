
/*

	skybox_pass_ps.hlsl
 
 */

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};

#include "include/math_util.hlsli"
#include "include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

struct CbSkyBox
{
	float	exposure;
	uint	panorama_mode;
    float   debug_mip_bias;
};
ConstantBuffer<CbSkyBox> cb_skybox;

// Cubemapモードで利用.
TextureCube tex_skybox_cube;
// パノラマモードで利用..
Texture2D tex_skybox_panorama;

SamplerState samp;

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
	float3 ray_vs = CalcViewSpaceRay(input.uv, ngl_cb_sceneview.cb_proj_mtx);
	float3 ray_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(ray_vs, 0));

	float4 tex_color = (float4)0;
	if(0 == cb_skybox.panorama_mode)
	{
		tex_color = tex_skybox_cube.SampleLevel(samp, ray_ws, cb_skybox.debug_mip_bias);
	}
	else
	{
		const float2 panorama_uv = CalcPanoramaTexcoordFromWorldSpaceRay(ray_ws);
		tex_color = tex_skybox_panorama.SampleLevel(samp, panorama_uv, cb_skybox.debug_mip_bias);
	}

	return tex_color * cb_skybox.exposure;
}

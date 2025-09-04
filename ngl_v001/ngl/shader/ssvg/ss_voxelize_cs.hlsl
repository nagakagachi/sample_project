
#if 0

ss_voxelize_cs.hlsl

ハードウェア深度バッファからリニア深度バッファを生成

#endif


#include "../include/math_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

struct DispatchParam
{
    int2 TexHardwareDepthSize;
};
ConstantBuffer<DispatchParam> cb_dispatch_param;

Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;

RWTexture2D<float4>	RWTexWork;


[numthreads(8, 8, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_dispatch_param.TexHardwareDepthSize.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

    float d = TexHardwareDepth.Load(int3(dtid.xy, 0)).r;
    float view_z = ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.x / (d * ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.y + ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.z);


    float3 pixel_pos_ws;
	{
		float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        pixel_pos_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));
	}

    RWTexWork[dtid.xy] = frac(float4(pixel_pos_ws, view_z));
}

#if 0

ハードウェア深度バッファからリニア深度バッファを生成

#endif



// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

Texture2D			TexHardwareDepth;
RWTexture2D<float>	RWTexLinearDepth;


[numthreads(8, 8, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	float d = TexHardwareDepth.Load(int3(dtid.xy, 0)).r;

	float view_z = ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.x / (d * ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.y + ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.z);

	RWTexLinearDepth[dtid.xy] = view_z;// 現状はViewZそのまま(1以上のワールド距離単位)
}

#if 0

generate_lineardepth_cs.hlsl

ハードウェア深度バッファからリニア深度バッファを生成

#endif



// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;
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

    // Samplerを使うテストなのであまり意味はない.
    #if 0
        uint w, h;
        TexHardwareDepth.GetDimensions(w, h);
        const float d_samp = TexHardwareDepth.SampleLevel(SmpHardwareDepth, dtid.xy / float2(w, h), 0).r;
        if(0.0 > d_samp)
            d = 0.0;
    #endif

	float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);

	RWTexLinearDepth[dtid.xy] = view_z;// 現状はViewZそのまま(1以上のワールド距離単位)
}
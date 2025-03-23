
// マテリアルパス定義テキストを記述するためのブロック.
// 記述の制限
//  <material_config> と </material_config> は行頭に記述され, 余分な改行やスペース等が入っていることは許可されない(Parseの簡易化のため).
#if 0
<material_config>
    <pass name="depth" />
    <pass name="gbuffer" />
    <pass name="d_shadow" />

    <vs_in name="NORMAL" optional="false" />
    <vs_in name="TANGENT" optional="true" />
    <vs_in name="BINORMAL" optional="true" />
    <vs_in name="TEXCOORD" index="0" optional="true" />

</material_config>
#endif

#include "../mtl_header.hlsli"


Texture2D tex_basecolor;
Texture2D tex_normal;
Texture2D tex_occlusion;
Texture2D tex_roughness;
Texture2D tex_metalness;
// sampler.
SamplerState samp_default;


MtlVsOutput MtlVsEntryPoint(MtlVsInput input)
{
    MtlVsOutput output = (MtlVsOutput)0;

    // テスト
    //output.position_offset_ws = input.normal_ws * abs(sin(ngl_cb_sceneview.cb_time_sec / 1.0f)) * 0.05;
    
    return output;
}

MtlPsOutput MtlPsEntryPoint(MtlPsInput input)
{
    const float4 mtl_base_color = tex_basecolor.Sample(samp_default, input.uv0);
#if 0
    const float3 mtl_normal = tex_normal.Sample(samp_default, input.uv0).rgb * 2.0 - 1.0;
#else
    const float2 mtl_normal_bc5_sample = tex_normal.Sample(samp_default, input.uv0).rg * 2.0 - 1.0;
	const float mtl_normal_bc5_z = sqrt(saturate(1.0 - dot(mtl_normal_bc5_sample, mtl_normal_bc5_sample)));
    const float3 mtl_normal = float3(mtl_normal_bc5_sample.x, mtl_normal_bc5_sample.y, mtl_normal_bc5_z);
#endif
    
    const float mtl_occlusion = tex_occlusion.Sample(samp_default, input.uv0).r;	// glTFでは別テクスチャでもチャンネルはORMそれぞれRGBになっている?.
    const float mtl_roughness = tex_roughness.Sample(samp_default, input.uv0).g;	// .
    const float mtl_metalness = tex_metalness.Sample(samp_default, input.uv0).b;	// .
	    
    const float occlusion = mtl_occlusion;
    const float roughness = mtl_roughness;
    const float metallic = mtl_metalness;
    const float surface_optional = 0.0;
    const float material_id = 0.0;

    #if defined(NGL_VS_IN_TANGENT0) && defined(NGL_VS_IN_BINORMAL0)
        // TangentFrameがある場合はNormalMapping.
        const float3 normal_ws = input.tangent_ws * mtl_normal.x + input.binormal_ws * mtl_normal.y + input.normal_ws * mtl_normal.z;
    #else
        // TangentFrameがない場合は頂点法線をそのまま出力.
        const float3 normal_ws = input.normal_ws;
    #endif

    const float3 emissive = float3(0.0, 0.0, 0.0);

    // マテリアル出力.
    MtlPsOutput output = (MtlPsOutput)0;
    {
        output.base_color = mtl_base_color.xyz;
        output.occlusion = occlusion;

        output.normal_ws = normal_ws;
        
        output.roughness = roughness;

        output.metalness = metallic;

        output.emissive = emissive;

        output.opacity = mtl_base_color.a;

        // デバッグ
        if(false)
        {
            output.roughness = 0.3;
            output.metalness = 1.0;
        }
    }

    return output;
}


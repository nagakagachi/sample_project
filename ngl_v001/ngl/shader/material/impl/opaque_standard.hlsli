/*
    opaque_standard.hlsli
    マテリアル個別コード. 
    通常不透明.
*

/*
    定義テキストを記述するための #if#endif ブロック.
        記述の制限
        <material_config> と </material_config> は行頭に記述され, 余分な改行やスペース等が入っていることは許可されない(Parseの簡易化のため).
*/
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

// 適切なコード生成のためにここでこのヘッダ自身をインクルードする.
#include "../mtl_pass_base_declare.hlsli"



Texture2D tex_basecolor;
Texture2D tex_normal;
Texture2D tex_occlusion;
Texture2D tex_roughness;
Texture2D tex_metalness;
// sampler.
SamplerState samp_default;


// 頂点入力の自由度を確保するために頂点入力定義とその取得, 変換はマテリアル側に記述する.
    struct VS_INPUT
    {
        uint   vertex_id    :	SV_VertexID;
        
        float3 pos		:	POSITION;
        // POSITION 以外はマテリアル毎に定義XMLで必要とするものを記述することでマクロ定義されて有効となる.
        #if defined(NGL_VS_IN_NORMAL)
            float3 normal	:	NORMAL;
        #endif
        #if defined(NGL_VS_IN_TANGENT)
            float3 tangent	:	TANGENT;
        #endif
        #if defined(NGL_VS_IN_BINORMAL)
            float3 binormal	:	BINORMAL;
        #endif
                
        #if defined(NGL_VS_IN_COLOR0)
            float2 color0		:	COLOR0;
        #endif
                
        #if defined(NGL_VS_IN_TEXCOORD0)
            float2 uv0		:	TEXCOORD0;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD1)
            float2 uv1		:	TEXCOORD1;
        #endif
    };
// Pass側頂点シェーダで呼び出される頂点情報生成関数. 頂点入力について自由度を確保するためにマテリアル側コードで記述することにしている.
//  有効/無効な頂点入力に関する処理を隠蔽する目的の整形関数.
    MaterialVertexAttributeData MaterialCallback_GetVertexAttributeData(VS_INPUT input)
    {
        MaterialVertexAttributeData output = (MaterialVertexAttributeData)0;
        
        output.pos = input.pos;
        
        #if defined(NGL_VS_IN_NORMAL)
            output.normal = normalize(input.normal);// TODO. コンバートあるいは読み込み時に正規化すればここの正規化は不要. 現状は簡易化のためここで実行.
        #endif
        #if defined(NGL_VS_IN_TANGENT)
            output.tangent = normalize(input.tangent);
        #endif
        #if defined(NGL_VS_IN_BINORMAL)
            output.binormal = normalize(input.binormal);
        #endif

        #if defined(NGL_VS_IN_COLOR0)
            output.color0 = input.color0;
        #endif
        
        #if defined(NGL_VS_IN_TEXCOORD0)
            output.uv0 = input.uv0;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD1)
            output.uv1 = input.uv1;
        #endif
        
        return output;
    }




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


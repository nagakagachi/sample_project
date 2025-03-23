#ifndef NGL_SHADER_MTL_HEADER_H
#define NGL_SHADER_MTL_HEADER_H

/*
    MaterialImplシェーダコードは先頭に以下のようなコメントブロックでXMLによる定義記述をする.
    ここにはこのマテリアルのシェーダを生成するPass名や, 頂点入力として要求するSemantic情報を記述する.
    基本的にPOSITION以外の頂点入力に関しては必要なものはすべて記述が必要とする.
    また頂点入力はシステムが規定した POSITION,NORMAL,TANGENT,BINORMAL,TEXCOORD0-3, COLOR0-3 の枠のみとする.

#if 0
<material_config>
    <pass name="depth"/>
    <pass name="gbuffer"/>

    <vs_in name="NORMAL"/>
    <vs_in name="TANGENT"/>
    <vs_in name="BINORMAL"/>
    <vs_in name="TEXCOORD" index="0"/ optional="true">
</material_config>
#endif
 
*/


// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )


// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

// ShadowPass向け
// Directional Cascade Shadow Rendering用 定数バッファ構造定義.
struct SceneDirectionalShadowRenderInfo
{
    float3x4 cb_shadow_view_mtx;
    float3x4 cb_shadow_view_inv_mtx;
    float4x4 cb_shadow_proj_mtx;
    float4x4 cb_shadow_proj_inv_mtx;
};
ConstantBuffer<SceneDirectionalShadowRenderInfo> ngl_cb_shadowview;


// ------------------------------------------------------
//  MaterialPassShader生成で NGL_VS_IN_[SEMANTIC][SEMANTIC_INDEX] のマクロが定義される.
    struct VS_INPUT
    {
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
        #if defined(NGL_VS_IN_COLOR1)
            float2 color1		:	COLOR1;
        #endif
        #if defined(NGL_VS_IN_COLOR2)
            float2 color2		:	COLOR2;
        #endif
        #if defined(NGL_VS_IN_COLOR3)
            float2 color3		:	COLOR3;
        #endif
                
        #if defined(NGL_VS_IN_TEXCOORD0)
            float2 uv0		:	TEXCOORD0;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD1)
            float2 uv1		:	TEXCOORD1;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD2)
            float2 uv2		:	TEXCOORD2;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD3)
            float2 uv3		:	TEXCOORD3;
        #endif
    };
    struct VS_OUTPUT
    {
        float4 pos		:	SV_POSITION;
        float2 uv0		:	TEXCOORD0;

        float3 pos_ws	:	POSITION_WS;
        float3 pos_vs	:	POSITION_VS;
	        
        float3 normal_ws	:	NORMAL_WS;
        float3 tangent_ws	:	TANGENT_WS;
        float3 binormal_ws	:	BINORMAL_WS;
    };

// ------------------------------------------------------
    // PassShaderが頂点入力にアクセスする際に利用する構造体.
    //  PassShaderは頂点入力に直接アクセスしない. 有効/無効な頂点入力に関する処理を隠蔽した ConstructVsInputWrapper() を使用して頂点入力情報にアクセスすること.
    struct VsInputWrapper
    {
        float3 pos;
        
        float3 normal;
        float3 tangent;
        float3 binormal;

        float4 color0;
        float4 color1;
        float4 color2;
        float4 color3;
        
        float2 uv0;
        float2 uv1;
        float2 uv2;
        float2 uv3;
    };
    // 有効/無効な頂点入力に関する処理を隠蔽する目的の整形関数.
    VsInputWrapper ConstructVsInputWrapper(VS_INPUT input)
    {
        VsInputWrapper output = (VsInputWrapper)0;
        
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
        #if defined(NGL_VS_IN_COLOR1)
                output.color1 = input.color1;
        #endif
        #if defined(NGL_VS_IN_COLOR2)
                output.color2 = input.color2;
        #endif
        #if defined(NGL_VS_IN_COLOR3)
                output.color3 = input.color3;
        #endif
        
        #if defined(NGL_VS_IN_TEXCOORD0)
            output.uv0 = input.uv0;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD1)
                output.uv1 = input.uv1;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD2)
                output.uv2 = input.uv2;
        #endif
        #if defined(NGL_VS_IN_TEXCOORD3)
                output.uv3 = input.uv3;
        #endif
        
        return output;
    }

// ------------------------------------------------------
    struct MtlVsInput
    {
        float3 position_ws;
        float3 normal_ws;
    };
    struct MtlVsOutput
    {
        float3 position_offset_ws;
    };

// ------------------------------------------------------
    struct MtlPsInput
    {
        float4  pos_sv;
        float2  uv0;

        float3  pos_ws;
        float3  pos_vs;
	        
        float3  normal_ws;
        float3  tangent_ws;
        float3  binormal_ws;
    };

    struct MtlPsOutput
    {
        float3  base_color;
        float   occlusion;
        float3  normal_ws;
        float   roughness;
        float3  emissive;
        float   metalness;
        float   opacity;
};

#endif



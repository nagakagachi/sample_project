#ifndef NGL_SHADER_MTL_PASS_DECLARE_H
#define NGL_SHADER_MTL_PASS_DECLARE_H

/*
    mtl_pass_base_declare.hlsli

    MaterialImplシェーダコード は先頭に以下のようなコメントブロックでXMLによる定義記述をする.
    ここにはこのマテリアルのシェーダを生成するPass名や, 頂点入力として要求するSemantic情報を記述する.
    基本的にPOSITION以外の頂点入力に関しては必要なものはすべて記述が必要とする.
    また頂点入力はシステムが規定した POSITION,NORMAL,TANGENT,BINORMAL,TEXCOORD0-3, COLOR0-3 の枠のみとする.
    
    MaterialPassShader生成で NGL_VS_IN_[SEMANTIC][SEMANTIC_INDEX] のマクロが定義されるため, 頂点入力の静的分岐などに利用できる.

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

// 適切なコード生成のためにここでこのヘッダ自身をインクルードする.
#include "../mtl_pass_base_declare.hlsli"
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
    // マテリアル側頂点シェーダが頂点入力を解釈してPass側頂点シェーダに返すための構造体.
    //  PassShaderは頂点入力に直接アクセスしない. 有効/無効な頂点入力に関する処理を隠蔽したりAttributeLessで構築されたこの構造体で頂点情報を得る.
    struct MaterialVertexAttributeData
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

// ------------------------------------------------------
    // Pass側VSの出力及びPSの入力.
    struct VS_OUTPUT
    {
        float4 pos		:	SV_POSITION;
        float2 uv0		:	TEXCOORD0;
        float4 color0	:   COLOR0;

        float3 pos_ws	:	POSITION_WS;
        float3 pos_vs	:	POSITION_VS;
            
        float3 normal_ws	:	NORMAL_WS;
        float3 tangent_ws	:	TANGENT_WS;
        float3 binormal_ws	:	BINORMAL_WS;
    };
        
// ------------------------------------------------------
    // マテリアル側のVSへの入力
    struct MtlVsInput
    {
        float3 position_ws;
        float3 normal_ws;
    };

    // マテリアル側のVSからの出力.
    struct MtlVsOutput
    {
        float3 position_offset_ws;
    };

// ------------------------------------------------------
    // マテリアル側のPSへの入力
    struct MtlPsInput
    {
        float4  pos_sv;
        float2  uv0;
        float4  color0;

        float3  pos_ws;
        float3  pos_vs;
	        
        float3  normal_ws;
        float3  tangent_ws;
        float3  binormal_ws;
    };

    // マテリアル側のPSからの出力.
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



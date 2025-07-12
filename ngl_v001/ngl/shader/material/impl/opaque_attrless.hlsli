/*
    opaque_attrless.hlsli
    マテリアル個別コード. 
    頂点Attribute不使用でShaderResourceによる頂点情報取り込み描画.
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

    <!-- アトリビュート無し -->

</material_config>
#endif

// 適切なコード生成のためにここでこのヘッダ自身をインクルードする.
#include "../mtl_pass_base_declare.hlsli"



Texture2D tex_basecolor;
// sampler.
SamplerState samp_default;



struct HalfEdge
{
    int twin   ;
    int next   ;
    int prev   ;
    int vertex ;
};
StructuredBuffer<HalfEdge> half_edge_buffer;

Buffer<float3>  vertex_position_buffer;



// 頂点入力の自由度を確保するために頂点入力定義とその取得, 変換はマテリアル側に記述する.
    struct VS_INPUT
    {
        uint   vertex_id    :	SV_VertexID;
        
        // アトリビュート無し.
    };
// Pass側頂点シェーダで呼び出される頂点情報生成関数. 頂点入力について自由度を確保するためにマテリアル側コードで記述することにしている.
//  有効/無効な頂点入力に関する処理を隠蔽する目的の整形関数.
    MaterialVertexAttributeData MaterialCallback_GetVertexAttributeData(VS_INPUT input)
    {
        MaterialVertexAttributeData output = (MaterialVertexAttributeData)0;
        
        const uint vertex_id = input.vertex_id;

    
            const float2 test_tri_uv[3] = {
                float2(0.0, 0.0),
                float2(1.0, 0.0),
                float2(0.5, 1.0)
            };

    
    
            const uint tri_index = vertex_id / 3;
            const uint local_index = vertex_id % 3;

            // HalfEdgeからTriangleVertexIndex取得.
            HalfEdge base_half_edge = half_edge_buffer[tri_index*3];
            uint3 tri_vertex_index;    
            tri_vertex_index.x = base_half_edge.vertex;
            tri_vertex_index.y = half_edge_buffer[base_half_edge.next].vertex;
            tri_vertex_index.z = half_edge_buffer[base_half_edge.prev].vertex;
    
            // ShaderResourceから頂点情報取得.
	        output.pos = vertex_position_buffer[tri_vertex_index[local_index]];
            output.normal = float3(0.0, 1.0, 0.0); // 上向きの法線.
            output.tangent = float3(1.0, 0.0, 0.0); // X軸方向の接線.
            output.binormal = float3(0.0, 0.0, 1.0); // Z軸方向の副接線.
            output.color0 = float4(1.0, 1.0, 1.0, 1.0); // 白色.
            output.uv0 = test_tri_uv[local_index]; // UV座標は矩形の頂点に対応.
    

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
    
    const float occlusion = 1.0; // アトリビュート無しなので常に1.0.
    const float roughness = 0.5; // アトリビュート無しなので常に0.5.
    const float metallic = 0.0; // アトリビュート無しなので常に0.0.
    const float surface_optional = 0.0;
    const float material_id = 0.0;

    const float3 normal_ws = input.normal_ws;

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


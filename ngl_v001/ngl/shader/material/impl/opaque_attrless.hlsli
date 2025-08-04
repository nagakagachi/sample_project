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

// 適切なコード生成のためにインクルード.
#include "../mtl_pass_base_declare.hlsli"

#include "../../sw_tess/bisector.hlsli"
// bisector関連のバッファ定義含む.
#include "../../sw_tess/cbt_tess_common.hlsli"


Texture2D tex_basecolor;
// sampler.
SamplerState samp_default;


uint3 iqint2_orig(uint3 x)  
{  
    uint k = 1103515245;  
  
    x = ((x >> 8) ^ x.yzx) * k;  
    x = ((x >> 8) ^ x.yzx) * k;  
    x = ((x >> 8) ^ x.yzx) * k;  
      
    return x;  
} 
float iqint2(float4 pos)  
{
    const uint4 bin_val = uint4(asuint(pos));
    return (dot(iqint2_orig(bin_val.xyz), 1) + dot(iqint2_orig(bin_val.w), 1)) * 2.3283064365386962890625e-10;
}


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
        const uint tri_index = vertex_id / 3;           // Bisectorキャッシュインデックス用
        const uint local_index = vertex_id % 3;         // Bisectorのローカル頂点インデックス（0, 1, 2）

        // CBTテッセレーション：index_cacheから有効なBisectorのインデックスを取得
        const uint bisector_index = index_cache[tri_index].x;
        
        // 処理対象のBisectorを取得
        Bisector bisector = bisector_pool[bisector_index];

        const uint3 local_tri_vtx_indices = (bisector.bs_depth & 1)? uint3(1, 0, 2) : uint3(0, 1, 2);

        // Bisectorの基本頂点インデックスを取得 (curr, next, prev)（共通関数を使用）
        int3 base_vertex_indices = CalcRootBisectorBaseVertex(bisector.bs_id, bisector.bs_depth);
        const uint base_triangle_hash = base_vertex_indices.x ^ 
                                       base_vertex_indices.y ^ 
                                       base_vertex_indices.z;

        // Bisectorの頂点属性補間マトリックスを計算（共通関数を使用）
        float3x3 attribute_matrix = CalcBisectorAttributeMatrix(bisector.bs_id, bisector.bs_depth);
        
        // 基本三角形の頂点座標を取得
        float3 v0_base = vertex_position_buffer[base_vertex_indices.x]; // curr
        float3 v1_base = vertex_position_buffer[base_vertex_indices.y]; // next  
        float3 v2_base = vertex_position_buffer[base_vertex_indices.z]; // prev
        
        // 属性マトリックスを使ってBisectorの頂点座標を計算
        float3x3 base_positions = float3x3(v0_base, v1_base, v2_base);
        float3x3 bisector_positions = mul(attribute_matrix, base_positions);
        
        // Bisectorの三角形頂点座標から適切な頂点を選択
        output.pos = bisector_positions[local_tri_vtx_indices[local_index]]; // local_index: 0=第1頂点, 1=第2頂点, 2=第3頂点
        
        // その他の属性設定
        // 仮のタンジェントフレーム.
        output.normal = normalize(cross(bisector_positions[local_tri_vtx_indices.y] - bisector_positions[local_tri_vtx_indices.x], bisector_positions[local_tri_vtx_indices.z] - bisector_positions[local_tri_vtx_indices.x]));
        output.tangent = normalize(bisector_positions[local_tri_vtx_indices.y] - bisector_positions[local_tri_vtx_indices.x]);
        output.binormal = normalize(cross(output.normal, output.tangent));
    
        // Bisector可視化：RootBisectorとBisectorIDを組み合わせた色生成
        uint bs_id_seed = bisector.bs_id + 1;
        
        float3 bisector_color;
        
        const float local_debug_color_seed = local_index;
            
            
            const float depth_color_rate = frac((float(bisector.bs_depth - cbt_mesh_minimum_tree_depth)) / 12.0);

            const float3 depth_color_test0 = lerp(float3(0.0, 0.0, 0.8), float3(0.0, 0.8, 0.0), saturate(depth_color_rate*2.0));
            const float3 depth_color_test1 = lerp(depth_color_test0, float3(1.0, 0.0, 0.0), saturate((depth_color_rate-0.5)*2.0));

            bisector_color.rgb = depth_color_test1;

            float selected_bisector_flag = 0.0;
            if(0 <= debug_target_bisector_id && 0 <= debug_target_bisector_depth)
            {
                if(debug_target_bisector_id == bisector.bs_id && debug_target_bisector_depth == bisector.bs_depth)
                {
                    // デバッグ対象のBisectorは選択されたフラグを立てる
                    selected_bisector_flag = 1.0;
                }
            }
            else if(0 <= debug_target_bisector_id || 0 <= debug_target_bisector_depth)
            {
                if(debug_target_bisector_id == bisector.bs_id || debug_target_bisector_depth == bisector.bs_depth)
                {
                    // デバッグ対象のBisectorは選択されたフラグを立てる
                    selected_bisector_flag = 1.0;
                }
            }


        output.uv1.x = selected_bisector_flag; // デバッグ用フラグ（選択されたBisectorかどうか）
        output.uv1.y = 0.0;
        
        output.color0 = float4(bisector_color, 1.0);
        output.color0.rgb = lerp(output.color0.rgb, float3(1.0, 0.0, 0.0), selected_bisector_flag);
        
        // UV座標の設定
        /*
        const float2 test_tri_uv[3] = {
            float2(0.0, 0.0),
            float2(1.0, 0.0),
            float2(0.5, 1.0)
        };
        output.uv0 = test_tri_uv[local_tri_vtx_indices[local_index]];
        */
        // 重心座標可視化用.
        const float2 test_tri_uv[3] = {
            float2(0.0, 0.0),
            float2(1.0, 0.0),
            float2(0.0, 1.0)
        };
        output.uv0 = test_tri_uv[local_tri_vtx_indices[local_index]];

        return output;
    }




MtlVsOutput MtlVsEntryPoint(MtlVsInput input)
{
    MtlVsOutput output = (MtlVsOutput)0;

    // テスト
    //output.position_offset_ws = input.normal_ws * abs(sin(ngl_cb_sceneview.cb_time_sec / 1.0f)) * 0.05;

    output.position_offset_ws += input.normal_ws * input.uv1.x*0.5;
    
    return output;
}

MtlPsOutput MtlPsEntryPoint(MtlPsInput input)
{
    // Bisector可視化：頂点カラーをベースカラーとして使用
    float4 mtl_base_color = input.color0;
    
    // 元のテクスチャサンプリング（コメントアウト）
    //float4 mtl_base_color = tex_basecolor.Sample(samp_default, input.uv0);
    {
        const float3 bary3 = float3(input.uv0, 1.0 - input.uv0.x - input.uv0.y);
        const float min_bary = min(bary3.x, min(bary3.y, bary3.z));

        mtl_base_color.xyz = (0.01 > min_bary) ? float3(1.0, 1.0, 1.0) : mtl_base_color.xyz; // 赤色で可視化
    }



    const float occlusion = 1.0; // アトリビュート無しなので常に1.0.
    const float roughness = 0.1; // アトリビュート無しなので常に0.5.
    const float metallic = 0.0; // アトリビュート無しなので常に0.0.
    const float surface_optional = 0.0;
    const float material_id = 0.0;

    const float3 normal_ws = input.normal_ws;

    const float3 emissive = float3(0.0, 0.0, 0.0);

    // マテリアル出力.
    MtlPsOutput output = (MtlPsOutput)0;
    {
        output.base_color = mtl_base_color.xyz; // Bisectorの色を出力
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


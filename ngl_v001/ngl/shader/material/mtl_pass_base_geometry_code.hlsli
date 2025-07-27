#ifndef NGL_SHADER_MTL_PASS_BASE_GEOMETRY_CODE_H
#define NGL_SHADER_MTL_PASS_BASE_GEOMETRY_CODE_H

/*
    mtl_pass_base_geometry_code.hlsli

    マテリアルシェーダの共通頂点シェーダ用コード.
    
    頂点入力 VS_INPUT はマテリアルコード側で定義する.
    同様に VS_INPUT を読み取って共通頂点データ構造MaterialVertexAttributeDataに変換する関数MaterialCallback_GetVertexAttributeDataをマテリアルコード側で定義する.

    // ------------------------------------------------------
    // 頂点入力の例. 頂点入力はマテリアル側コードで定義する.
    //  頂点入力や, AttributeLess頂点シェーダなどに対応するためにマテリアル側で定義する設計.
    //  MaterialPassShader生成で NGL_VS_IN_[SEMANTIC][SEMANTIC_INDEX] のマクロが定義される.
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

    // マテリアル側での頂点入力を隠蔽して共通頂点データ構造へ変換する関数.
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
    */


// -------------------------------------------------------------------------------------------
// VS.  Pass共通コード. マテリアル固有コードやコールバックなどをでカスタマイズされる.
    VS_OUTPUT main_vs(VS_INPUT input)
    {
        // Material Customize Point.
        //  VS_INPUTとそこから共通頂点データ取得をする関数MaterialCallback_GetVertexAttributeDataをマテリアル側で定義することで
        //  マテリアル毎に自由な頂点入力ができるようになっている.
        MaterialVertexAttributeData input_wrap = MaterialCallback_GetVertexAttributeData(input);
        
        const float3x4 instance_mtx = NglGetInstanceTransform(0);
        const float3x4 instance_mtx_cofactor = NglGetInstanceTransformCofactor(0);
        
        float3 pos_ws = mul(instance_mtx, float4(input_wrap.pos, 1.0)).xyz;
        float3 pos_vs = mul(ngl_cb_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
        float4 pos_cs = mul(ngl_cb_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));

        // TangetnFrameの内, Normalは逆転置行列transpose(inverse(M)) または 余因子行列cofactor(M) で変換する.
        //      https://github.com/graphitemaster/normals_revisited
        //      https://stackoverflow.com/questions/13654401/why-transform-normals-with-the-transpose-of-the-inverse-of-the-modelview-matrix
        float3 normal_ws = normalize(mul(instance_mtx_cofactor, float4(input_wrap.normal, 0.0)).xyz);
        float3 tangent_ws = normalize(mul(instance_mtx, float4(input_wrap.tangent, 0.0)).xyz);
        float3 binormal_ws = normalize(mul(instance_mtx, float4(input_wrap.binormal, 0.0)).xyz);

        float2 uv0 = input_wrap.uv0;

        float4 color0 = input_wrap.color0;

        // マテリアル側頂点計算コード呼び出し.
        MtlVsInput mtl_input = (MtlVsInput)0;
        {
            mtl_input.position_ws = pos_ws;
            mtl_input.normal_ws = normal_ws;
        }
        // Material Customize Point.
        // マテリアル側の頂点オフセット操作.
        MtlVsOutput mtl_output = MtlVsEntryPoint(mtl_input);


        // ---------------------------------------------------------------
        // Pass毎に少し異なる処理.
        #if defined(NGL_SHADER_MTL_PASS_D_SHADOW)
            // ShadowPass用.
            {
                pos_ws = pos_ws + mtl_output.position_offset_ws;
                // 再計算.
                // Shadowの場合はMaterial計算用はSceneView, 最終的な変換はShadowViewで変換する.
                pos_vs = mul(ngl_cb_shadowview.cb_shadow_view_mtx, float4(pos_ws, 1.0));
                pos_cs = mul(ngl_cb_shadowview.cb_shadow_proj_mtx, float4(pos_vs, 1.0));
            }
        #else
            // DepthPrePass, GBUfferPass用.
            {
                pos_ws = pos_ws + mtl_output.position_offset_ws;
                // 再計算.
                pos_vs = mul(ngl_cb_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
                pos_cs = mul(ngl_cb_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));
            }
        #endif
        // ---------------------------------------------------------------
        

        // PSへの出力構造構築.
        VS_OUTPUT output = (VS_OUTPUT)0;
        {
            output.pos = pos_cs;
            output.uv0 = uv0;
            output.color0 = color0;

            output.pos_ws = pos_ws;
            output.pos_vs = pos_vs;

            output.normal_ws = normal_ws;
            output.tangent_ws = tangent_ws;
            output.binormal_ws = binormal_ws;
        }
	    
        return output;
    }



#endif



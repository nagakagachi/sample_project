/*
マテリアル実装hlsliで定義された関数を呼び出すPassシェーダコード.
    マテリアルシェーダコード生成の仕組みによって, generatedディレクトリにマテリアル実装hlsliやこのファイルをincludeしたシェーダファイルが作られる.
*/

#include "../mtl_header.hlsli"
#include "../mtl_instance_transform_buffer.hlsli"



// -------------------------------------------------------------------------------------------
// VS.
    VS_OUTPUT main_vs(VS_INPUT input)
    {
        VsInputWrapper input_wrap = ConstructVsInputWrapper(input);
        
        const float3x4 instance_mtx = NglGetInstanceTransform(0);
        const float3x4 instance_mtx_cofactor = NglGetInstanceTransformCofactor(0);
        
        float3 pos_ws = mul(instance_mtx, float4(input_wrap.pos, 1.0)).xyz;
        float3 pos_vs = mul(ngl_cb_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
        float4 pos_cs = mul(ngl_cb_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));

        // TangetnFrameの内, Normalは逆転置行列transpose(inverse(M)) または 余因子行列cofactor(M) で変換する.
        //  TangentとBinormalは表面の平面上のベクトルであるため, Normalのように歪んだスケールの打消が不要のため元の行列Mで変換する.
        //  
        //  これは非均一スケールやミラーのスケールを含む変換でその表面の法線を適切に変換するための標準的な方法 (XYZ均一スケールでは元の行列Mでも問題ない).
        //      https://github.com/graphitemaster/normals_revisited
        //      https://stackoverflow.com/questions/13654401/why-transform-normals-with-the-transpose-of-the-inverse-of-the-modelview-matrix
        float3 normal_ws = normalize(mul(instance_mtx_cofactor, float4(input_wrap.normal, 0.0)).xyz);
        float3 tangent_ws = normalize(mul(instance_mtx, float4(input_wrap.tangent, 0.0)).xyz);
        float3 binormal_ws = normalize(mul(instance_mtx, float4(input_wrap.binormal, 0.0)).xyz);

        float2 uv0 = input_wrap.uv0;
        
        MtlVsInput mtl_input = (MtlVsInput)0;
        {
            mtl_input.position_ws = pos_ws;
            mtl_input.normal_ws = normal_ws;
        }
        // マテリアル処理呼び出し.
        MtlVsOutput mtl_output = MtlVsEntryPoint(mtl_input);
        {
            pos_ws = pos_ws + mtl_output.position_offset_ws;
            // 再計算.
            pos_vs = mul(ngl_cb_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
            pos_cs = mul(ngl_cb_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));
        }
        
        VS_OUTPUT output = (VS_OUTPUT)0;
        {
            output.pos = pos_cs;
            output.uv0 = uv0;

            output.pos_ws = pos_ws;
            output.pos_vs = pos_vs;

            output.normal_ws = normal_ws;
            output.tangent_ws = tangent_ws;
            output.binormal_ws = binormal_ws;
        }
	    
        return output;
    }


// -------------------------------------------------------------------------------------------
// PS.
    struct GBufferOutput
    {
        float4 gbuffer0 : SV_TARGET0;
        float4 gbuffer1 : SV_TARGET1;
        float4 gbuffer2 : SV_TARGET2;
        float4 gbuffer3 : SV_TARGET3;
        float2 velocity : SV_TARGET4;
    };

    GBufferOutput main_ps(VS_OUTPUT input)
    {
        MtlPsInput mtl_input = (MtlPsInput)0;
        {
            mtl_input.pos_sv = input.pos;
            mtl_input.uv0 = input.uv0;
            
            mtl_input.pos_ws = input.pos_ws;
            mtl_input.pos_vs = input.pos_vs;
            
            mtl_input.normal_ws = normalize(input.normal_ws);
            mtl_input.tangent_ws = normalize(input.tangent_ws);
            mtl_input.binormal_ws = normalize(input.binormal_ws);
        }

        // マテリアル処理呼び出し.
        MtlPsOutput mtl_output = MtlPsEntryPoint(mtl_input);
        
        GBufferOutput output = (GBufferOutput)0;
        // GBuffer Encode.
        {
            const float surface_optional = 0.0;
            const float material_id = 0.0;
            
            output.gbuffer0.xyz = mtl_output.base_color;
            output.gbuffer0.w = mtl_output.occlusion;

            // [-1,+1]のNormal を unorm[0,1]で格納.
            output.gbuffer1.xyz = mtl_output.normal_ws * 0.5 + 0.5;
            output.gbuffer1.w = 0.0;

            output.gbuffer2 = float4(mtl_output.roughness, mtl_output.metalness, surface_optional, material_id);

            output.gbuffer3 = float4(mtl_output.emissive, 0.0);
	    
            output.velocity = float2(0.0, 0.0);// velocityは保留.
        }
	    
        return output;
    }

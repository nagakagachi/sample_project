/*
    gbuffer.hlsli

    マテリアル実装hlsliで定義された関数を呼び出すPassシェーダコード.
    マテリアルシェーダコード生成の仕組みによって, generatedディレクトリにマテリアル実装hlsliやこのファイルをincludeしたシェーダファイルが作られる.
*/

// 生成されたコード側でPassを識別するために任意マクロ定義.
#define NGL_SHADER_MTL_PASS_GBUFFER

#include "../mtl_instance_transform_buffer.hlsli"


// -------------------------------------------------------------------------------------------
// VS.
    #include "..\mtl_pass_base_geometry_code.hlsli"

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

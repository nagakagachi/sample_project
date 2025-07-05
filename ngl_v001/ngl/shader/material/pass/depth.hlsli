/*
    depth.hlsli

    マテリアル実装hlsliで定義された関数を呼び出すPassシェーダコード.
    マテリアルシェーダコード生成の仕組みによって, generatedディレクトリにマテリアル実装hlsliやこのファイルをincludeしたシェーダファイルが作られる.
*/

// 生成されたコード側でPassを識別するために任意マクロ定義.
#define NGL_SHADER_MTL_PASS_DEPTH

#include "../mtl_pass_base_declare.hlsli"
#include "../mtl_instance_transform_buffer.hlsli"


// -------------------------------------------------------------------------------------------
// VS.
    #include "..\mtl_pass_base_geometry_code.hlsli"

// -------------------------------------------------------------------------------------------
// PS.
    void main_ps(VS_OUTPUT input)
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
        
        // TODO.
        //	アルファテストが必要なら実行.
        clip((0.0 >= mtl_output.opacity)? -1 : 1);
        
    }

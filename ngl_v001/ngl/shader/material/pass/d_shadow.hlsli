/*
    d_shadow.hlsli

    マテリアル実装hlsliで定義された関数を呼び出すPassシェーダコード.
    マテリアルシェーダコード生成の仕組みによって, generatedディレクトリにマテリアル実装hlsliやこのファイルをincludeしたシェーダファイルが作られる.
*/

// 生成されたコード側でPassを識別するために任意マクロ定義.
#define NGL_SHADER_MTL_PASS_D_SHADOW

#include "../mtl_instance_transform_buffer.hlsli"
#include "../mtl_pass_base_pixel_code.hlsli"


// -------------------------------------------------------------------------------------------
// VS.
    #include "..\mtl_pass_base_geometry_code.hlsli"

// -------------------------------------------------------------------------------------------
// PS.
    void main_ps(VS_OUTPUT input)
    {
        MtlPsInput mtl_input = GenerateMtlPsInputFromVsOutput(input);

        // Material Customize Point.
        // マテリアル側ピクセル処理.
        MtlPsOutput mtl_output = MtlPsEntryPoint(mtl_input);
        
        // TODO.
        //	アルファテストが必要なら実行.
        clip((0.0 >= mtl_output.opacity)? -1 : 1);
        
    }

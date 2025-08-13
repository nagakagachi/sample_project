#ifndef NGL_SHADER_MTL_PASS_BASE_PIXEL_CODE_H
#define NGL_SHADER_MTL_PASS_BASE_PIXEL_CODE_H

/*
    mtl_pass_base_pixel_code.hlsli

    マテリアルシェーダの共通ピクセルシェーダ用コード.
*/

    // 頂点シェーダからの入力 VS_OUTPUT からマテリアルシェーダコードのピクセルシェーダ入力データを構成.
    MtlPsInput GenerateMtlPsInputFromVsOutput(VS_OUTPUT input)
    {
        MtlPsInput mtl_input = (MtlPsInput)0;
        {
            mtl_input.pos_sv = input.pos;
            mtl_input.uv0 = input.uv0;
            mtl_input.color0 = input.color0;
            
            mtl_input.pos_ws = input.pos_ws;
            mtl_input.pos_vs = input.pos_vs;
            
            mtl_input.normal_ws = normalize(input.normal_ws);
            mtl_input.tangent_ws = normalize(input.tangent_ws);
            mtl_input.binormal_ws = normalize(input.binormal_ws);
        }
        return mtl_input;
    }


#endif



// material_shader_generator.h

#pragma once

//#include <tinyxml2.h>
//#include "text/hash_text.h"


namespace ngl
{
namespace gfx
{
    class MaterialShaderFileGenerator
    {
    public:
        MaterialShaderFileGenerator() = default;
        ~MaterialShaderFileGenerator();

        //  material_impl_dir : マテリアル毎の実装hlsliのディレクトリ
        //  material_pass_dir : マテリアルのPassシェーダhlsliのディレクトリ(depth_pass.hlsli, gbuffer_pass.hlsli, etc.)
        //  generated_root_dir : マテリアルシェーダの出力先ルートディレクトリ. ここに マテリアル名/マテリアル毎のPassシェーダ群 が生成される.
        bool GenerateMaterialShaderFiles(const char* material_impl_dir, const char* material_pass_dir, const char* generated_root_dir);
        
    private:
        
    };
}
}

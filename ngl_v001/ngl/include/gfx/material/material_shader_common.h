// material_shader_common.h

#pragma once

#include <array>
#include <string>

#include "text/hash_text.h"

namespace ngl::gfx
{
namespace mtl
{
    static constexpr char k_material_config_xml_begin_tag[] = "<material_config>";
    static constexpr char k_material_config_xml_end_tag[] = "</material_config>";
        
    static constexpr text::HashText<32> k_material_config_pass_tag = "pass";
    static constexpr text::HashText<32> k_material_config_vs_in_tag = "vs_in";
    static constexpr text::HashText<32> k_material_config_vs_in_optional_attr = "optional";

    // 生成シェーダに記述される有効頂点入力セマンティクスマクロのPrefix. ex #define NGL_VS_IN_POSITION .
    constexpr text::HashText<32> k_material_generate_code_macro_vs_in_prefix = "NGL_VS_IN_";

    // 生成シェーダのファイル名としてShaderStageとして記述されるステージ名.
    // 現状は固定で VS-PS.
    static const std::array<std::string,2> k_generate_file_stage_names =
    {
        "vs",
        "ps"
    };

    // 生成シェーダのファイル名に含まれる有効頂点セマンティクススロットマスク値のPrefix. mtl.pass.vsin_1234.ps.hlsli .
    static constexpr text::HashText<32> k_generate_file_vsin_prefix = "vsin_";
    
}
}

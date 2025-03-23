// material_shader_generator.cpp

#include "gfx/material/material_shader_generator.h"
#include "file/file.h"

#include <array>
#include <filesystem>
#include <fstream>

// xml操作.
#include <iostream>
#include <tinyxml2.h>
#include <unordered_map>

#include "gfx/common_struct.h"

#include "gfx/material/material_shader_common.h"

namespace ngl
{
namespace gfx
{
    MaterialShaderFileGenerator::~MaterialShaderFileGenerator()
    {   
    }

    /*
     // マテリアル実装hlsliにコメント等でXMLによるマテリアル定義が記述されているものとしてそれをParseする.
     // Parseの簡易化のために開始終了の <material_config>, </material_config> は行頭で不要なスペースや改行が無いものとする.
     // 要求する頂点入力と, それらがOptionalで存在しないVariationも生成するかを指定できる.(POSITIONは既定で必ず存在するものとする).
     
        <material_config>
            <pass name="depth"/>
            <pass name="gbuffer"/>
            
            <vs_in name="NORMAL" optional="true" />
            <vs_in name="TANGENT" optional="true" />
            <vs_in name="BINORMAL" optional="true" />
            <vs_in name="TEXCOORD" index="0" optional="true" />
        </material_config>
    */
    bool MaterialShaderFileGenerator::GenerateMaterialShaderFiles(const char* material_impl_dir, const char* material_pass_dir, const char* generated_root_dir)
    {
        struct PassShaderInfo
        {
            // ./shader/material/pass\depth.hlsli, etc.
            std::string path = {};
            // depth, gbuffer, etc.
            std::string name = {};
        };
        struct MaterialConfig
        {
            // ./shader/material/impl\opaque_standard.hlsli
            std::string impl_file_path = {};
            // opaque_standard
            std::string name = {};
            // depth, gbuffer, etc.
            std::vector<std::string> pass = {};
            
            // 要求頂点入力情報.
            std::vector<EMeshVertexSemanticKind::Type> vs_in_semantic_type = {};
            std::vector<int> vs_in_semantic_index = {};
            std::vector<bool> vs_in_optional = {};
        };

        // パスチェック.
        std::filesystem::path impl_dir_path(material_impl_dir);
        std::filesystem::path pass_dir_path(material_pass_dir);
        std::filesystem::path generated_dir_path(generated_root_dir);
        if(!std::filesystem::is_directory(impl_dir_path))
        {
            assert(false);
            return false;
        }
        if(!std::filesystem::is_directory(pass_dir_path))
        {
            assert(false);
            return false;
        }
        if(!std::filesystem::is_directory(generated_dir_path))
        {
            assert(false);
            return false;
        }

        
        // 生成用収集データ.
        std::vector<PassShaderInfo> pass_shader_info_list = {};
        std::unordered_map<std::string, int> pass_shader_info_map = {};// pass name -> pass_shader_info_list index.
        std::vector<MaterialConfig> material_config_list = {};
        
        // Passシェーダ情報収集.
        for(auto it : std::filesystem::directory_iterator(pass_dir_path))
        {
            if(it.is_directory())
                continue;

            if(0 == it.path().extension().compare(".hlsli"))
            {
                const auto pass_name = it.path().stem().string();
                
                pass_shader_info_map[pass_name] = static_cast<int>(pass_shader_info_list.size());// name->index.
                pass_shader_info_list.push_back({});

                auto& pass_shader_info = pass_shader_info_list.back();
                {
                    pass_shader_info.path = it.path().string();
                    pass_shader_info.name = pass_name;
                }
            }
        }

        // Materialシェーダ情報収集.
        for(auto it : std::filesystem::directory_iterator(impl_dir_path))
        {
            if(it.is_directory() || !it.path().has_stem())
                continue;

            material_config_list.push_back({});
            auto& mtl_config = material_config_list.back();
            {
                mtl_config.impl_file_path = it.path().string();
                mtl_config.name = it.path().filename().stem().string();
                mtl_config.pass = {};
            }
        }
        for(size_t i = 0; i < material_config_list.size(); ++i)
        {
            // impl.hlsliを1行ずつReadして定義XML部を取り出す.
            std::string mtl_config_xml_buf = {};
            {
                enum EReadMtlCofigXmlStatus
                {
                    SEARCH,
                    FOUND,
                    END,
                };
                std::ifstream ifs(material_config_list[i].impl_file_path);
                if(ifs.is_open())
                {
                    EReadMtlCofigXmlStatus status = EReadMtlCofigXmlStatus::SEARCH;
                
                    std::string line_buf;
                    while(std::getline(ifs, line_buf))
                    {
                        if(EReadMtlCofigXmlStatus::SEARCH == status)
                        {
                            if(0 == line_buf.compare(mtl::k_material_config_xml_begin_tag))
                                status = EReadMtlCofigXmlStatus::FOUND;
                        }
                        else if(EReadMtlCofigXmlStatus::FOUND == status)
                        {
                            if(0 == line_buf.compare(mtl::k_material_config_xml_end_tag))
                                status = EReadMtlCofigXmlStatus::END;
                        }
                        else
                        {
                            break;
                        }
                        if(EReadMtlCofigXmlStatus::SEARCH != status)
                        {
                            mtl_config_xml_buf += line_buf;// XML部分を抽出.
                        }
                    }

                    // 正常にXML部読み取り出来ていない.
                    if(EReadMtlCofigXmlStatus::END != status)
                    {
                        std::cout << "[ERROR] Invalid  Material Config XML. " << material_config_list[i].name << std::endl;
                        mtl_config_xml_buf = {};// 失敗しているのでクリア.
                    }
                }
            }

            // XML部の解析.
            // mtl_config_xml_buf をXMLとして解釈してShaderFile生成情報取得.
            tinyxml2::XMLDocument xml_doc;
            if(tinyxml2::XML_SUCCESS == xml_doc.Parse(mtl_config_xml_buf.c_str()))
            {
                auto* xml_root = xml_doc.RootElement();
                // Root直下巡回.
                auto* xml_pass_tag = xml_root->FirstChildElement();
                while(xml_pass_tag)
                {
                    if(0 == strncmp(xml_pass_tag->Name(), mtl::k_material_config_pass_tag.Get(), mtl::k_material_config_pass_tag.Length()))
                    {
                        // Passタグ.
                        if(auto* pass_name = xml_pass_tag->Attribute("name"))
                        {
                            material_config_list[i].pass.push_back(pass_name);
                        }
                    }
                    else if(0 == strncmp(xml_pass_tag->Name(), mtl::k_material_config_vs_in_tag.Get(), mtl::k_material_config_vs_in_tag.Length()))
                    {
                        // 要求頂点入力タグ.
                        std::string semantic_name = "";
                        std::string semantic_index = "";
                        bool is_optional = false;
                        if(auto* name = xml_pass_tag->Attribute("name"))
                        {
                            semantic_name = name;
                        }
                        if(auto* index = xml_pass_tag->Attribute("index"))
                        {
                            semantic_index = index;
                        }
                        // Optionalかどうか.
                        is_optional = xml_pass_tag->BoolAttribute(mtl::k_material_config_vs_in_optional_attr.Get(), false);

                        const auto semantic_type = MeshVertexSemantic::ConvertSemanticNameToType(semantic_name.c_str());
                        if(EMeshVertexSemanticKind::Type::_MAX > semantic_type)
                        {
                            int semantic_index_i = 0;
                            if(!semantic_index.empty())
                            {
                                semantic_index_i = stoi(semantic_index);
                            }
                            material_config_list[i].vs_in_semantic_index.push_back(semantic_index_i);
                            material_config_list[i].vs_in_semantic_type.push_back(semantic_type);
                            material_config_list[i].vs_in_optional.push_back(is_optional);
                        }
                    }
                    
                    // to next.
                    xml_pass_tag = xml_pass_tag->NextSiblingElement();
                }
            }
        }

        struct MaterialPassShaderSeed
        {
            int material_index = -1;    // to material_config_list index.
            int pass_index = -1;        // to pass_shader_info_list index.

            // 頂点入力パターンやVariationパターン等も入るかも.
            MeshVertexSemanticSlotMask  vs_in_slot_mask = {};
        };
        using ShaderSeedList = std::vector<MaterialPassShaderSeed>;
        std::vector<ShaderSeedList> per_mtl_shader_seed_list = {};
        per_mtl_shader_seed_list.resize(material_config_list.size());
        
        for(size_t mtl_i = 0; mtl_i < material_config_list.size(); ++mtl_i)
        {
            const auto& mtl_config = material_config_list[mtl_i];
            // Passが存在しないものはありえない.
            if( 0 >= mtl_config.pass.size())
                continue;

            for(size_t pass_i = 0; pass_i < mtl_config.pass.size(); ++pass_i)
            {
                auto pass_it = pass_shader_info_map.find(mtl_config.pass[pass_i]);
                if(pass_shader_info_map.end() == pass_it)
                    continue;

                
                std::vector<MeshVertexSemanticSlotMask> vs_in_pattern = {};
                {
                    MeshVertexSemanticSlotMask vsin_slot_mask_base = {};
                    vsin_slot_mask_base.AddSlot(EMeshVertexSemanticKind::POSITION);// POSITIONは既定.
                    std::vector<int> vsin_opt_index_list = {};
                    
                    for(size_t vsin_i = 0; vsin_i < mtl_config.vs_in_optional.size(); ++vsin_i)
                    {
                        // 有無のパターンがある頂点入力の処理.
                        if(mtl_config.vs_in_optional[vsin_i])
                        {
                            // 有無のパターンが要求される頂点入力情報のインデックスのみリスト化.
                            vsin_opt_index_list.push_back(static_cast<int>(vsin_i));
                        }
                        else
                        {
                            // 必須の頂点入力のマスクを構築.
                            vsin_slot_mask_base.AddSlot(mtl_config.vs_in_semantic_type[vsin_i], mtl_config.vs_in_semantic_index[vsin_i]);
                        }
                    }
                    assert(32 > vsin_opt_index_list.size());// 念の為.

                    // Optionalの全ビットパターンを巡回してMaskを生成.
                    const u32 pattern_count = 1u << vsin_opt_index_list.size();
                    for(u32 pi = 0; pi < pattern_count; ++pi)
                    {
                        MeshVertexSemanticSlotMask vsin_slot_mask_opt = vsin_slot_mask_base;// 必須SlotのMaskをベースとする.
                        for(u32 pj = 0; pj < vsin_opt_index_list.size(); ++pj)
                        {
                            const auto vsin_opt_index = vsin_opt_index_list[pj];
                            if((pi >> pj) & 0x01)
                                vsin_slot_mask_opt.AddSlot(mtl_config.vs_in_semantic_type[vsin_opt_index], mtl_config.vs_in_semantic_index[vsin_opt_index]);
                        }
                        vs_in_pattern.push_back(vsin_slot_mask_opt);
                    }
                }

                // 頂点入力パターン分生成.
                for(size_t vsin_pattern_i = 0; vsin_pattern_i < vs_in_pattern.size(); ++vsin_pattern_i)
                {
                    // シード追加.
                    per_mtl_shader_seed_list[mtl_i].push_back({});
                    MaterialPassShaderSeed& seed = per_mtl_shader_seed_list[mtl_i].back();
                    {
                        seed.material_index = static_cast<int>(mtl_i);
                        seed.pass_index = pass_it->second;
                        seed.vs_in_slot_mask = vs_in_pattern[vsin_pattern_i];
                    }
                }
            }
        }
        
        // 収集した情報から generated へMaterialPassシェーダを出力する.
        for(size_t mtl_i = 0; mtl_i < per_mtl_shader_seed_list.size(); ++mtl_i)
        {
            const auto& per_mtl_seed = per_mtl_shader_seed_list[mtl_i];
            if(0 >= per_mtl_seed.size())
                    continue;

            std::filesystem::path mtl_pass_shader_dir_path = generated_dir_path;
            mtl_pass_shader_dir_path += "/" + material_config_list[mtl_i].name;

            // 既存のMaterialディレクトリ削除.
            if(is_directory(mtl_pass_shader_dir_path))
            {
                std::filesystem::remove_all(mtl_pass_shader_dir_path);// 内部ファイルもすべて削除.
            }
            // Materialディレクトリ再生成.
            std::filesystem::create_directories(mtl_pass_shader_dir_path);

            for(const auto& pass_seed : per_mtl_seed)
            {
                const int mtl_index = static_cast<int>(pass_seed.material_index);
                const int pass_index = static_cast<int>(pass_seed.pass_index);
                const auto vs_in_slot_mask = pass_seed.vs_in_slot_mask;

                // material.pass.vsin_mask
                const std::string shader_base_name =
                    material_config_list[mtl_index].name
                    + "." + pass_shader_info_list[pass_index].name
                    + "." + mtl::k_generate_file_vsin_prefix.Get() + std::to_string(vs_in_slot_mask.mask)
                ;

                // Stage毎に生成.
                for(const auto& stage : mtl::k_generate_file_stage_names)
                {   
                    // material.pass.stage
                    const std::string stage_shader_name = shader_base_name + "." + stage;
                    
                    std::filesystem::path shader_path = mtl_pass_shader_dir_path;
                    shader_path += "/" + stage_shader_name + ".hlsl";
                    {
                        // ファイル生成.
                        std::ofstream ofs(shader_path);
                        assert(ofs.is_open());

                        // 生成Shaderディレクトリからの相対パス.
                        std::filesystem::path rel_impl_path = std::filesystem::path(material_config_list[mtl_index].impl_file_path).lexically_relative(mtl_pass_shader_dir_path);
                        std::filesystem::path rel_pass_path = std::filesystem::path(pass_shader_info_list[pass_index].path).lexically_relative(mtl_pass_shader_dir_path);

                        // コード部生成.
                        /*
                            #define NGL_VS_IN_POSITION0
                            #define NGL_VS_IN_POSITION
                            #define NGL_VS_IN_NORMAL0
                            #define NGL_VS_IN_NORMAL
                            #include "../../impl/opaque_standard.hlsli"
                            #include "../../pass/depth.hlsli"
                        */

                        // 頂点入力セマンティクス.
                        {
                            u32 vs_in_mask = vs_in_slot_mask.mask;
                            for(int vsin_slot_i = 0;0 < vs_in_mask; ++vsin_slot_i)
                            {
                                const auto check_mask = (1u<<vsin_slot_i);
                                if(check_mask&vs_in_mask)
                                {
                                    const auto slot_semantic_type = MeshVertexSemantic::k_semantic_slot_info.slot_semantic_type[vsin_slot_i];
                                    const auto slot_semantic_index = MeshVertexSemantic::k_semantic_slot_info.slot_semantic_index[vsin_slot_i];
                                    std::string require_semantic_name = MeshVertexSemantic::SemanticNameStr(slot_semantic_type);

                                    ofs << "#define "
                                    << mtl::k_material_generate_code_macro_vs_in_prefix.Get()
                                    << require_semantic_name
                                    << slot_semantic_index
                                    << "\n";
                                
                                    if(0 == slot_semantic_index)
                                    {
                                        // Index==0の場合はIndexを省略したバージョンも記述しておく.
                                        ofs << "#define "
                                        << mtl::k_material_generate_code_macro_vs_in_prefix.Get()
                                        << require_semantic_name
                                        << "\n";
                                    }

                                    // 処理ビット除去.
                                    vs_in_mask &= ~(check_mask);
                                }
                            }
                        }
                        
                        // include部.
                        {
                            // generic_string() で / 区切りに変換.
                            ofs << "#include " << "\"" << rel_impl_path.generic_string() << "\"" << "\n";
                            ofs << "#include " << "\"" << rel_pass_path.generic_string() << "\"" << "\n";
                        }

                        // Complete.
                        ofs.close();
                    }
                }
            }
        }

        return true;
    }
    
}
}

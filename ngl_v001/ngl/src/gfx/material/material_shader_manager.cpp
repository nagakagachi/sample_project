// material_shader_manager.cpp

#include "gfx/material/material_shader_manager.h"

#include <filesystem>

#include "gfx/material/material_shader_common.h"

#include "gfx/resource/resource_shader.h"
#include "resource/resource_manager.h"
#include "rhi/d3d12/device.d3d12.h"
#include "util/bit_operation.h"

namespace ngl
{
namespace gfx
{
    // MaterialPassShaderファイル名の.区切りパーツそれぞれの意味を定義するENUM.
    //  MaterialName.PassName.ShaderStage.hlsl
    //      ShaderStageは特殊で末尾(.hlsliの一つ前)にあるものとしている.
    enum EMaterialShaderNamePart
    {
        MATERIAL_NAME,
        PASS_NAME,

        _MAX
    };
    struct MaterialPassShaderSet
    {
        std::string pass_name = {};
            
        std::string vs_file = {};
        std::string ps_file = {};

        res::ResourceHandle<ResShader> res_vs = {};
        res::ResourceHandle<ResShader> res_ps = {};

        // 頂点シェーダが要求するInputSemanticsMask.
        MeshVertexSemanticSlotMask vs_in_slot_mask = {};
    };
    struct MaterialShaderSet
    {
        std::string material_name = {};

        // 完全一致でシェーダを検索.
        // セットアップ時のシェーダデータベースから検索する際に利用.
        int FindPerfectMatching(const char* pass_name, MeshVertexSemanticSlotMask vs_in_slot) const
        {
            
            // 現状は辞書化せずに探索. キーが確定したら辞書化を検討.
            for(size_t i = 0; i < pass_shader_set.size(); ++i)
            {
                const auto& e = pass_shader_set[i];
                if(e.pass_name != pass_name)
                    continue;
                if(e.vs_in_slot_mask.mask != vs_in_slot.mask)
                    continue;

                return static_cast<int>(i);// 発見.
            }
            return -1;
        }
        // 完全一致ではなく, 少なくともシェーダ側が要求するスロットがvs_in_slotにも存在するものを検索.
        //  ランタイムでのMesh描画用シェーダ検索に利用.
        int FindMatching(const char* pass_name, MeshVertexSemanticSlotMask vs_in_slot) const
        {
            // 現状は辞書化せずに探索. キーが確定したら辞書化を検討.
            int max_match_bit = -1;
            int max_match_index = -1;
            for(size_t i = 0; i < pass_shader_set.size(); ++i)
            {
                const auto& e = pass_shader_set[i];
                if(e.pass_name != pass_name)
                    continue;
                
                const auto match_mask = (e.vs_in_slot_mask.mask & vs_in_slot.mask);
                // 少なくともシェーダ側が要求するマスクがvs_in_slot側にあるものだけ通過する.
                if(e.vs_in_slot_mask.mask != match_mask)
                    continue;

                // 完全一致ではなくても, vs_in_slotのスロットをなるべく使用するものを選択する.
                // 一致ビットの個数が最大のものにしてみる. 本来はslot毎に優先度もありそうだが...
                const int match_bit = CountbitAutoType(e.vs_in_slot_mask.mask);
                if(max_match_bit < match_bit)
                {
                    max_match_bit = match_bit;
                    max_match_index = static_cast<int>(i);
                }
            }
            return max_match_index;
        }
            
        std::vector<MaterialPassShaderSet> pass_shader_set = {};
    };
    
    struct MaterialPassPso
    {
        std::string pass_name = {};
        rhi::RhiRef<rhi::GraphicsPipelineStateDep> ref_pso = {};
        // 頂点シェーダが要求するInputSemanticsMask.
        MeshVertexSemanticSlotMask vs_in_slot_mask = {};
    };
    struct MaterialPassPsoSet
    {
        ~MaterialPassPsoSet()
        {
            for(auto& e : pso_lib)
            {
                if(e)
                {
                    delete e;
                    e = {};
                }
            }
            pso_lib.clear();
        }

        int FindMatching(const char* pass_name, MeshVertexSemanticSlotMask vs_in_slot) const
        {
#if 1
            // 完全一致ではなくても, vs_in_slotのスロットをなるべく使用するものを選択する.
            
            // 現状は辞書化せずに探索. キーが確定したら辞書化を検討.
            for(size_t i = 0; i < pso_lib.size(); ++i)
            {
                const auto& e = pso_lib[i];
                if(e->pass_name != pass_name)
                    continue;
                
                const auto match_mask = (e->vs_in_slot_mask.mask & vs_in_slot.mask);
                // 少なくともシェーダ側が要求するマスクがvs_in_slot側にあるものだけ通過する.
                if(e->vs_in_slot_mask.mask != match_mask)
                    continue;

                // TODO.

                return static_cast<int>(i);// 発見.
            }
            return -1;
#else
            // 完全一致でPSOを検索.

            // 現状は辞書化せずに探索. キーが確定したら辞書化を検討.
            for(size_t i = 0; i < pso_lib.size(); ++i)
            {
                const auto& e = pso_lib[i];
                if(e->pass_name != pass_name)
                    continue;
                if(e->vs_in_slot_mask.mask != vs_in_slot.mask)
                    continue;

                return static_cast<int>(i);// 発見.
            }
            return -1;
#endif
        }
        
        std::string material_name = {};
        std::vector<MaterialPassPso*> pso_lib = {};
        
        std::mutex pso_lib_mutex_ = {};// Material別のLcok用.
    };

    // 隠蔽用.
    class MaterialShaderManagerImpl
    {
    public:
        // ShaderData検索.
        const MaterialPassShaderSet* FindPassShaderSet(const char* material_name, const char* pass_name, MeshVertexSemanticSlotMask vs_in_slot) const
        {
            // Material検索.
            const auto mtl_shader_it = material_shader_name_index_.find(material_name);
            if(material_shader_name_index_.end() == mtl_shader_it)
                return {};

            // Pass検索.
            const auto& pass_set = material_shader_lib_[mtl_shader_it->second];
            
            const int find_index = pass_set.FindMatching(pass_name, vs_in_slot);
            if(0 <= find_index)
                return &pass_set.pass_shader_set[find_index];

            return {};
        }
        
        
        void Cleanup()
        {
            material_shader_lib_ = {};
            material_shader_name_index_ = {};
            {
                for(auto& e : material_pso_lib_)
                {
                    if(e)
                    {
                        delete e;
                        e = {};
                    }
                }
                material_pso_lib_.clear();
            }
            material_pso_name_index_ = {};
        }
        std::vector<MaterialShaderSet> material_shader_lib_ = {};
        std::unordered_map<std::string, int> material_shader_name_index_ = {};
 
        std::vector<MaterialPassPsoSet*> material_pso_lib_ = {};
        std::unordered_map<std::string, int> material_pso_name_index_ = {};

        std::mutex material_pso_lib_mutex_ = {};// Material MapのLock用.
    };

    
    MaterialShaderManager::MaterialShaderManager()
    {
        p_impl_ = new MaterialShaderManagerImpl();
    }
    MaterialShaderManager::~MaterialShaderManager()
    {
        if(p_impl_)
        {
            delete p_impl_;
            p_impl_ = {};
        }
    }

    //  generated_shader_root_dir : マテリアルシェーダディレクトリ. ここに マテリアル名/マテリアル毎のPassシェーダ群 が生成される.
    bool MaterialShaderManager::Setup(rhi::DeviceDep* p_device, const char* generated_shader_root_dir)
    {
        assert(p_device);
        p_device_ = p_device;
        
        
        std::filesystem::path root_dir_path = generated_shader_root_dir;
        if(!exists(root_dir_path) || !is_directory(root_dir_path))
        {
            assert(false);
            return false;
        }


        auto& material_shader_set = p_impl_->material_shader_lib_;
        auto& material_shader_name_index = p_impl_->material_shader_name_index_;

        // 直下のマテリアル別ディレクトリ巡回.
        for(auto mtl_dir_it : std::filesystem::directory_iterator(root_dir_path))
        {
            if(!mtl_dir_it.is_directory())
                continue;

            const std::string mtl_name = mtl_dir_it.path().filename().string();

            for(auto shader_it : std::filesystem::directory_iterator(mtl_dir_it))
            {
                std::filesystem::path shader_file_name_path = shader_it.path().filename();
                if(0 != shader_file_name_path.extension().compare(".hlsl"))
                    continue;

                // shader_filename_splitは . で区切られて各種識別名を含んでいる.
                // 先頭2つは Material名.Pass名
                // 末尾は ShaderStage名
                // 中間には頂点入力タイプやVariation情報を含む予定
                //   例　opaque_standard.gbuffer.ps 
                auto shader_filename = shader_file_name_path.stem().string();
                std::vector<std::string> shader_filename_split = {};
                int split_begin = 0;
                int split_pos = 0;
                for(split_pos = 0; split_pos < shader_filename.size(); ++split_pos)
                {
                    if('.' == shader_filename[split_pos])
                    {
                        shader_filename_split.push_back(shader_filename.substr(split_begin, split_pos - split_begin));
                        split_begin = split_pos + 1;
                    }
                }
                if(split_begin < split_pos)
                {
                    // 最後の要素.
                    shader_filename_split.push_back(shader_filename.substr(split_begin, split_pos - split_begin));
                }

                // ファイルから取得した情報数チェック.
                //  最低でも MaterialName PassName ShaderStage の3つは存在する.
                if(3 > shader_filename_split.size())
                {
                    assert(false);
                    continue;
                }

                // ファイル名から情報収集.
                const size_t k_stage_name_split_index = shader_filename_split.size()-1;
                const std::string material_name = shader_filename_split[EMaterialShaderNamePart::MATERIAL_NAME];
                const std::string pass_name = shader_filename_split[EMaterialShaderNamePart::PASS_NAME];
                const std::string shader_stage_name = shader_filename_split[k_stage_name_split_index];// ShaderStage(vs,ps)は末尾.
                MeshVertexSemanticSlotMask vsin_slot_from_filename = {};
                for(int split_i = EMaterialShaderNamePart::_MAX; split_i < k_stage_name_split_index; ++split_i)
                {
                    // vsin mask.
                    if(0 == shader_filename_split[split_i].compare(0, mtl::k_generate_file_vsin_prefix.Length(), mtl::k_generate_file_vsin_prefix.Get()))
                    {
                        std::string vsin_mask_part = shader_filename_split[split_i].substr(mtl::k_generate_file_vsin_prefix.Length());// prefixを除いた部分取得.
                        vsin_slot_from_filename.mask = stoi(vsin_mask_part);
                    }
                }

                // Material毎のデータベース.
                if(material_shader_name_index.end() == material_shader_name_index.find(material_name))
                {
                    // name -> index.
                    material_shader_name_index[material_name] = static_cast<int>(material_shader_set.size());
                    material_shader_set.push_back({});
                    auto& new_mtl_shader_set = material_shader_set.back();
                    {
                        // 新規要素初期化.
                        new_mtl_shader_set.material_name = material_name;
                    }
                }
                const int mtl_index = material_shader_name_index[material_name];
                auto& mtl_shader_set = material_shader_set[mtl_index];


                // Pass毎のシェーダセット.
                if(0 > mtl_shader_set.FindPerfectMatching(pass_name.c_str(), vsin_slot_from_filename))
                {
                    mtl_shader_set.pass_shader_set.push_back({});
                    auto& new_pass_shader_set = mtl_shader_set.pass_shader_set.back();
                    {
                        // 新規要素初期化.
                        new_pass_shader_set.pass_name = pass_name;
                        new_pass_shader_set.vs_in_slot_mask = vsin_slot_from_filename;
                    }
                }
                const int pass_shader_set_index = mtl_shader_set.FindPerfectMatching(pass_name.c_str(), vsin_slot_from_filename);
                auto& pass_shader_set = mtl_shader_set.pass_shader_set[pass_shader_set_index];

                // Passを構成するStage毎のShader設定.
                {
                    if(shader_stage_name == "vs")
                    {
                        pass_shader_set.vs_file = shader_it.path().generic_string();// generic_string で / 区切りパスとする.
                    }
                    else if(shader_stage_name == "ps")
                    {
                        pass_shader_set.ps_file = shader_it.path().generic_string();// generic_string で / 区切りパスとする.
                    }
                    else
                    {
                        assert(false);
                    }
                }
            }
        }
        
        // Shaderロード.
        static constexpr char k_shader_model[] = "6_3";
		auto& ResourceMan = ngl::res::ResourceManager::Instance();
        for(size_t mtl_i = 0; mtl_i < material_shader_set.size(); ++mtl_i)
        {
            auto& mtl_set = material_shader_set[mtl_i];

            for(size_t pass_i = 0; pass_i < mtl_set.pass_shader_set.size(); ++pass_i)
            {
                auto& pass_set = mtl_set.pass_shader_set[pass_i];

                if(0 < pass_set.vs_file.size())
                {
                    ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
                    loaddesc_vs.entry_point_name = "main_vs";
                    loaddesc_vs.stage = ngl::rhi::EShaderStage::Vertex;
                    loaddesc_vs.shader_model_version = k_shader_model;

                    pass_set.res_vs = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, pass_set.vs_file.c_str(), &loaddesc_vs);

                    // InputElementのMaskを構築. ShaderReflection利用. 事前生成してもよいかも.
                    MeshVertexSemanticSlotMask vs_in_mask = {};
                    {
                        rhi::ShaderReflectionDep shader_ref;
                        if(shader_ref.Initialize(p_device_, &pass_set.res_vs->data_))
                        {
                            for(u32 ii = 0; ii < shader_ref.NumInputParamInfo(); ++ii)
                            {
                                EMeshVertexSemanticKind::Type semantic = MeshVertexSemantic::ConvertSemanticNameToType(shader_ref.GetInputParamInfo(ii)->semantic_name);
                                if(EMeshVertexSemanticKind::_MAX > semantic)
                                    vs_in_mask.AddSlot(semantic, shader_ref.GetInputParamInfo(ii)->semantic_index);
                            }
                        }
                    }
                    pass_set.vs_in_slot_mask = vs_in_mask;
                }
                if(0 < pass_set.vs_file.size())
                {
                    ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
                    loaddesc_vs.entry_point_name = "main_ps";
                    loaddesc_vs.stage = ngl::rhi::EShaderStage::Pixel;
                    loaddesc_vs.shader_model_version = k_shader_model;

                    pass_set.res_ps = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, pass_set.ps_file.c_str(), &loaddesc_vs);
                }
            }
        }

        // 実際のPSO生成はリクエストされた段階で実行する方式. -> CreateMaterialPipeline().
        
        return true;
    }

    void MaterialShaderManager::Finalize()
    {
        p_impl_->Cleanup();
        registered_pass_pso_creator_map_ = {};
        p_device_ = {};
    }

    void MaterialShaderManager::RegisterPassPsoCreator(const char* name, IMaterialPassPsoCreator* p_instance)
    {
        if(registered_pass_pso_creator_map_.end() != registered_pass_pso_creator_map_.find(name))
        {
            // 二重登録はエラー.
            assert(false);
            return;
        }
        registered_pass_pso_creator_map_.insert( std::make_pair(name, p_instance));
        // Pass名は別途リスト化.
        registered_pass_name_list_.push_back(name);
    }

    MaterialPsoSet MaterialShaderManager::GetMaterialPsoSet(const char* material_name, MeshVertexSemanticSlotMask vsin_slot)
    {
        MaterialPsoSet ret = {};
        for(int pass_i = 0; pass_i < registered_pass_name_list_.size(); ++pass_i)
        {
            if(auto* p_pso = CreateMaterialPipeline(material_name, registered_pass_name_list_[pass_i].c_str(), vsin_slot))
            {
                ret.pass_name_list.push_back(registered_pass_name_list_[pass_i]);
                ret.p_pso_list.push_back(p_pso);
            }
            else{
                // PSO生成失敗.
                assert(false && "MaterialShaderManager::GetMaterialPsoSet() : CreateMaterialPipeline failed.");
            }
        }
        return ret;
    }
    // マテリアル名と追加情報からPipeline生成またはCacheから取得.
    rhi::GraphicsPipelineStateDep* MaterialShaderManager::CreateMaterialPipeline(const char* material_name, const char* pass_name, MeshVertexSemanticSlotMask vsin_slot)
    {
        // 無効なPassの場合はnullptr.
        if(registered_pass_pso_creator_map_.end() == registered_pass_pso_creator_map_.find(pass_name))
        {
            return {};
        }
        
        // Material検索.
        MaterialPassPsoSet* p_mtl_pso_set = {};
        {
            // 最上位のMapをLock.
            std::lock_guard<std::mutex> lock(p_impl_->material_pso_lib_mutex_);

            const auto mtl_it = p_impl_->material_pso_name_index_.find(material_name);
            if(p_impl_->material_pso_name_index_.end() != mtl_it)
            {
                p_mtl_pso_set = p_impl_->material_pso_lib_[mtl_it->second];
            }
            else
            {
                // Mapに未登録なら新規追加.
                const int new_index = static_cast<int>(p_impl_->material_pso_lib_.size());
                p_impl_->material_pso_name_index_[material_name] = new_index;
                p_impl_->material_pso_lib_.push_back(new MaterialPassPsoSet());// vector拡張時にアドレス変わらないようにnew.(mutex lockの範囲を狭める都合.
                {
                    auto& new_elem = p_impl_->material_pso_lib_.back();
                    new_elem->material_name = material_name;
                }
                p_mtl_pso_set = p_impl_->material_pso_lib_[new_index];
            }
        }

        {
            // Material別のLock. 排他範囲が重なりにくくしたい意図.
            std::lock_guard<std::mutex> lock(p_mtl_pso_set->pso_lib_mutex_);

            // 生成済みMaterialPsoSetから検索. vs_inの完全一致だと用意されていないシェーダのvs_inパターンが足りないため, 可能な限り一致するものを検索する.
            int find_match_pso_index = p_mtl_pso_set->FindMatching(pass_name, vsin_slot);
            if(0 <= find_match_pso_index)
            {
                // Cacheにあれば即座に返却.
                return p_mtl_pso_set->pso_lib[find_match_pso_index]->ref_pso.Get();
            }
            else
            {
                // 未登録なら新規生成と登録.
                rhi::RhiRef<rhi::GraphicsPipelineStateDep> ref_pso = {};
                MeshVertexSemanticSlotMask  vs_require_input_mask = {};
                {
                    // このMeshを描画可能なシェーダバリエーションを検索. 完全一致ではシェーダデータベース側に存在しない可能性があるため保守的な検索でヒットしたものを利用.
                    auto* shader_set = p_impl_->FindPassShaderSet(material_name, pass_name, vsin_slot);
                    if(!shader_set)
                        return {};
                    MaterialPassPsoDesc pso_desc = {};
                    {
                        pso_desc.p_vs = shader_set->res_vs.Get();
                        pso_desc.p_ps = shader_set->res_ps.Get();

                        // InputLayoutMask.
                        pso_desc.vs_input_layout_mask = shader_set->vs_in_slot_mask;
                        // TODO. option.
                    }
                    // Passに対応したCreatorで生成.
                    ref_pso = registered_pass_pso_creator_map_[pass_name]->Create(p_device_, pso_desc);
                    // VS要求入力マスク.
                    vs_require_input_mask = shader_set->vs_in_slot_mask;
                }
                if(!ref_pso.IsValid())
                {
                    assert(false);
                    return {};
                }
                
                p_mtl_pso_set->pso_lib.push_back(new MaterialPassPso());// vector拡張時にアドレス変わらないようにnew.(mutex lockの範囲を狭める都合.
                auto& new_elem = p_mtl_pso_set->pso_lib.back();
                {
                    new_elem->pass_name = pass_name;
                    new_elem->ref_pso = ref_pso;
                    new_elem->vs_in_slot_mask = vs_require_input_mask;
                }
                // 返却.
                return new_elem->ref_pso.Get();
            }
        }
    }

    // VS Input Semantic MaskからInputElementを生成.
    //  本来は対応するMeshのSemanticに対応するBufferのFormatを参照すべきだが, とりあえずSemantic毎に固定されているものとして記述.
    template<typename INPUT_ELEMENT_ARRAY>
    void SetupInputElementArrayDefault(INPUT_ELEMENT_ARRAY& inout_input_elem_data, int& out_num_element, MeshVertexSemanticSlotMask vs_input_mask)
    {
        int elem_index = 0;
        auto mask = vs_input_mask.mask;
        for(int i = 0; 0 != mask; ++i)
        {
            if(mask & (1u << i))
            {
                const auto semantic_type = MeshVertexSemantic::k_semantic_slot_info.slot_semantic_type[i];
                const auto semantic_index = MeshVertexSemantic::k_semantic_slot_info.slot_semantic_index[i];
            
                inout_input_elem_data[elem_index].semantic_name = ngl::gfx::MeshVertexSemantic::SemanticNameStr(semantic_type);
                inout_input_elem_data[elem_index].semantic_index = semantic_index;
                inout_input_elem_data[elem_index].stream_slot = ngl::gfx::MeshVertexSemantic::SemanticSlot(semantic_type, semantic_index);
                inout_input_elem_data[elem_index].element_offset = 0;// 非Interleavedなバッファ前提なのでオフセット無し.

                // TODO. formatはMesh側から引かないとわからないが,とりあえず固定パターン.
                switch(semantic_type)
                {
                case ngl::gfx::EMeshVertexSemanticKind::POSITION:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R32G32B32_FLOAT;
                        break;
                    }
                case ngl::gfx::EMeshVertexSemanticKind::NORMAL:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R32G32B32_FLOAT;
                        break;
                    }
                case ngl::gfx::EMeshVertexSemanticKind::TANGENT:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R32G32B32_FLOAT;
                        break;
                    }
                case ngl::gfx::EMeshVertexSemanticKind::BINORMAL:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R32G32B32_FLOAT;
                        break;
                    }
                case ngl::gfx::EMeshVertexSemanticKind::TEXCOORD:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R32G32_FLOAT;
                        break;
                    }
                case ngl::gfx::EMeshVertexSemanticKind::COLOR:
                    {
                        inout_input_elem_data[elem_index].format = ngl::rhi::EResourceFormat::Format_R8G8B8A8_UNORM;
                        break;   
                    }
                default:
                    {
                        assert(false);
                        break;
                    }
                }

                mask &= ~(1u << i);// ビットを0に.
                ++elem_index;
            }
        }

        out_num_element = elem_index;// 個数書き込み.
    }
    
    // Depth Pass用PSO生成.
    rhi::GraphicsPipelineStateDep* MaterialPassPsoCreator_depth::Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc)
    {   
        ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
        desc.vs = &pass_pso_desc.p_vs->data_;
        desc.ps = &pass_pso_desc.p_ps->data_;

        desc.depth_stencil_state.depth_enable = true;
        desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
        desc.depth_stencil_state.depth_write_enable = true;
        desc.depth_stencil_state.stencil_enable = false;
        desc.depth_stencil_format = k_depth_format;

        // 入力レイアウト
        std::array<ngl::rhi::InputElement, 16> input_elem_data;// 最大数は適当.
        {
            int elem_index = 0;
            SetupInputElementArrayDefault(input_elem_data, elem_index, pass_pso_desc.vs_input_layout_mask);

            desc.input_layout.p_input_elements = input_elem_data.data();
            desc.input_layout.num_elements = static_cast<ngl::u32>(elem_index);
        }
        
        // PSO生成.
        auto p_pso = new rhi::GraphicsPipelineStateDep();
        if (!p_pso->Initialize(p_device, desc))
        {
            assert(false);
            return {};
        }
        return p_pso;
    }
    // GBuffer Pass用PSO生成.
    rhi::GraphicsPipelineStateDep* MaterialPassPsoCreator_gbuffer::Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc)
    {   
		ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
		desc.vs = &pass_pso_desc.p_vs->data_;
		desc.ps = &pass_pso_desc.p_ps->data_;
		{
			desc.num_render_targets = 5;
			desc.render_target_formats[0] = k_gbuffer0_format;
			desc.render_target_formats[1] = k_gbuffer1_format;
			desc.render_target_formats[2] = k_gbuffer2_format;
			desc.render_target_formats[3] = k_gbuffer3_format;
			desc.render_target_formats[4] = k_velocity_format;
		}
		{
			desc.depth_stencil_state.depth_enable = true;
			desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Equal; // Maskedを含めたEarlyZ Full PreZのためEqual.
			desc.depth_stencil_state.depth_write_enable = false;// 描き込み無効. 深度テストのみで書き込み無効.
			desc.depth_stencil_state.stencil_enable = false;
			desc.depth_stencil_format = k_depth_format;
		}
        
        // 入力レイアウト
        std::array<ngl::rhi::InputElement, 16> input_elem_data;// 最大数は適当.
        {
            int elem_index = 0;
            SetupInputElementArrayDefault(input_elem_data, elem_index, pass_pso_desc.vs_input_layout_mask);

            desc.input_layout.p_input_elements = input_elem_data.data();
            desc.input_layout.num_elements = static_cast<ngl::u32>(elem_index);
        }

        // PSO生成.
        auto p_pso = new rhi::GraphicsPipelineStateDep();
        if (!p_pso->Initialize(p_device, desc))
        {
            assert(false);
            return {};
        }
        return p_pso;
    }
    // DirectionalShadow Pass用PSO生成.
    rhi::GraphicsPipelineStateDep* MaterialPassPsoCreator_d_shadow::Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc)
    {   
        ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
        desc.vs = &pass_pso_desc.p_vs->data_;
        desc.ps = &pass_pso_desc.p_ps->data_;

        desc.depth_stencil_state.depth_enable = true;
        desc.depth_stencil_state.depth_func = ngl::rhi::ECompFunc::Greater; // ReverseZ.
        desc.depth_stencil_state.depth_write_enable = true;
        desc.depth_stencil_state.stencil_enable = false;
        desc.depth_stencil_format = k_depth_format;

        // 入力レイアウト
        std::array<ngl::rhi::InputElement, 16> input_elem_data;// 最大数は適当.
        {
            int elem_index = 0;
            SetupInputElementArrayDefault(input_elem_data, elem_index, pass_pso_desc.vs_input_layout_mask);

            desc.input_layout.p_input_elements = input_elem_data.data();
            desc.input_layout.num_elements = static_cast<ngl::u32>(elem_index);
        }
        
        // PSO生成.
        auto p_pso = new rhi::GraphicsPipelineStateDep();
        if (!p_pso->Initialize(p_device, desc))
        {
            assert(false);
            return {};
        }
        return p_pso;
    }
    
}
}

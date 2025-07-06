/*
    material_shader_manager.h

    // Materialシェーダコードは以下のように記述する.
    //  MaterialShaderFileGenerator によってPass毎のMaterialShaderFileが生成される.
    
        // マテリアルパス定義テキストを記述するためのコメントブロック.
        // 記述の制限
        //  <material_config> と </material_config> は行頭に記述され, 余分な改行やスペース等が入っていることは許可されない(Parseの簡易化のため).
        #if 0
        <material_config>
            <pass name="depth"/>
            <pass name="gbuffer"/>
        </material_config>
        #endif

        // 適切なコード生成のためにここでこのヘッダ自身をインクルードする.
        #include "../mtl_pass_base_declare.hlsli"

        // Material側の VertexShader関数.
        MtlVsOutput MtlVsEntryPoint(MtlVsInput input)
        {
            MtlVsOutput output = (MtlVsOutput)0;
            // TODO.
            return output;
        }

        // Material側の PixelShader関数.
        MtlPsOutput MtlPsEntryPoint(MtlPsInput input)
        {
            MtlPsOutput output = (MtlPsOutput)0;
            // TODO.
            return output;
        }


    // Passシェーダ
    //  MaterialShaderCodeを呼び出すPass毎のエントリポイントを含むシェーダ.
    //  エントリポイントは main_[ステージ名] とする.
    //      例r main_vs
    
        VS_OUTPUT main_vs(VS_INPUT input)
        {
            // ...
            // MaterialのVertexShader関数を呼び出す
            MtlVsOutput mtl_output = MtlVsEntryPoint(mtl_input);
            // ...
        }
        void main_ps(VS_OUTPUT input)
        {
            // ...
            // PixelShader関数を呼び出す
            MtlPsOutput mtl_output = MtlPsEntryPoint(mtl_input);
            // ...
        }
    
*/
#pragma once
#include <ostream>
#include <string>
#include <unordered_map>
#include <iostream>

#include "gfx/common_struct.h"
#include "rhi/rhi.h"
#include "util/singleton.h"

namespace ngl::gfx
{
    class ResShader;
}

namespace ngl::rhi
{
    class GraphicsPipelineStateDep;
    class DeviceDep;
}

namespace ngl
{
namespace gfx
{
    struct MaterialPassPsoDesc
    {
        const ResShader* p_vs = {};
        const ResShader* p_ps = {};

        MeshVertexSemanticSlotMask  vs_input_layout_mask = {};
    };
    // Pass Pso Creator Interface.
    //  Pass毎のPso生成クラスはこのクラスを継承する.
    class IMaterialPassPsoCreator
    {
    public:
        virtual ~IMaterialPassPsoCreator() = default;
        virtual rhi::GraphicsPipelineStateDep* Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc);
    };


    
    // -----------------------------------------------------------------------------------------------------------------------------------------------------

    // Depth Pass Pso Creator.
    class MaterialPassPsoCreator_depth : public IMaterialPassPsoCreator
    {
    public:
        // Pass識別名, 検索に利用するためマテリアルシェーダなどに記述するPass名と一致している必要がある.
        static constexpr char k_name[] = "depth";
        rhi::GraphicsPipelineStateDep* Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc) override;
        
        // Depth.
        static constexpr auto k_depth_format = rhi::EResourceFormat::Format_D32_FLOAT;
        
    private:
    };

    // GBUffer Pass Pso Creator.
    class MaterialPassPsoCreator_gbuffer : public IMaterialPassPsoCreator
    {
    public:
        // Pass識別名, 検索に利用するためマテリアルシェーダなどに記述するPass名と一致している必要がある.
        static constexpr char k_name[] = "gbuffer";
        rhi::GraphicsPipelineStateDep* Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc) override;
        
        // Depth.
        static constexpr auto k_depth_format = MaterialPassPsoCreator_depth::k_depth_format;// rhi::EResourceFormat::Format_D32_FLOAT;
        // GBuffer0 BaseColor, Occlusion
        static constexpr auto k_gbuffer0_format = rhi::EResourceFormat::Format_R8G8B8A8_UNORM_SRGB;
        // GBuffer1 WorldNormal.xyz, 1bitOption.w
        static constexpr auto k_gbuffer1_format = rhi::EResourceFormat::Format_R10G10B10A2_UNORM;
        // GBuffer2 Roughness, Metallic, Optional, MaterialId
        static constexpr auto k_gbuffer2_format = rhi::EResourceFormat::Format_R8G8B8A8_UNORM;
        // GBuffer3 Emissive.xyz, Unused.w
        static constexpr auto k_gbuffer3_format = rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
        // Velocity xy
        static constexpr auto k_velocity_format = rhi::EResourceFormat::Format_R16G16_FLOAT;
    private:
    };

    // DirectionalShadow Pass Pso Creator.
    class MaterialPassPsoCreator_d_shadow : public IMaterialPassPsoCreator
    {
    public:
        // Pass識別名, 検索に利用するためマテリアルシェーダなどに記述するPass名と一致している必要がある.
        static constexpr char k_name[] = "d_shadow";
        rhi::GraphicsPipelineStateDep* Create(rhi::DeviceDep* p_device, const MaterialPassPsoDesc& pass_pso_desc) override;
        
        // Depth.
        static constexpr auto k_depth_format = rhi::EResourceFormat::Format_D32_FLOAT;
        
    private:
    };

    
    // -----------------------------------------------------------------------------------------------------------------------------------------------------



    
    // Material Instance毎のPsoをまとめて取得するためのオブジェクト.
    struct MaterialPsoSet
    {
        // Pass名でPsoを取得.
        rhi::GraphicsPipelineStateDep* GetPassPso(const char* pass_name) const
        {
            auto find_id = std::find(pass_name_list.begin(), pass_name_list.end(), pass_name);
            if(pass_name_list.end() == find_id)
                return {};
            return p_pso_list[std::distance(pass_name_list.begin(), find_id)];
        }
        
        std::vector<std::string> pass_name_list;
        std::vector<rhi::GraphicsPipelineStateDep*> p_pso_list;
    };
    
    // ランタイムでMaterialShaderPSOの問い合わせに対応するクラス.
    class MaterialShaderManager : public Singleton<MaterialShaderManager>
    {
    public:
        MaterialShaderManager();
        ~MaterialShaderManager();

        // PassPso生成クラスを登録する.
        //  Setup()よりも先に登録が必要.
        template<typename PASS_PSO_CREATOR_TYPE>
        void RegisterPassPsoCreator();
        
        //  generated_shader_root_dir : マテリアルシェーダディレクトリ. ここに マテリアル名/マテリアル毎のPassシェーダ群 が生成される.
        bool Setup(rhi::DeviceDep* p_device, const char* generated_shader_root_dir);
        void Finalize();

        // マテリアルを構成するPassPsoセットを取得する. まだ生成されていない場合は内部で生成.
        MaterialPsoSet GetMaterialPsoSet(const char* material_name, MeshVertexSemanticSlotMask vsin_slot);

        const std::vector<std::string>& GetRegisteredPassNameList() const { return registered_pass_name_list_; }
    private:
        // マテリアル名と追加情報からPipeline生成またはCacheから取得.
        rhi::GraphicsPipelineStateDep* CreateMaterialPipeline(const char* material_name, const char* pass_name, MeshVertexSemanticSlotMask vsin_slot);

    private:
        void RegisterPassPsoCreator(const char* name, IMaterialPassPsoCreator* p_instance);
        // Pass Pso Creator登録用.
        std::unordered_map<std::string, IMaterialPassPsoCreator*> registered_pass_pso_creator_map_;
        std::vector<std::string>    registered_pass_name_list_;
    private:
        rhi::DeviceDep* p_device_ = {};
        // 内部データ隠蔽.
        class MaterialShaderManagerImpl* p_impl_ = {};
    };
    template<typename PASS_PSO_CREATOR_TYPE>
    inline void MaterialShaderManager::RegisterPassPsoCreator()
    {
        std::cout << "[MaterialShaderManager] RegisterPassPsoCreator " << PASS_PSO_CREATOR_TYPE::k_name << std::endl;

        // Pass Pso CreatorはStateを持たないため, 外部からは型だけ指定して内部でstaticインスタンスを登録する.
        static PASS_PSO_CREATOR_TYPE register_instance = {}; 
        RegisterPassPsoCreator(PASS_PSO_CREATOR_TYPE::k_name, &register_instance);
    }
}
}

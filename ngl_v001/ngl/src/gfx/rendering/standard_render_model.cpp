/*
    standard_render_model.cpp
*/
#include "gfx/rendering/standard_render_model.h"

#include "gfx/material/material_shader_manager.h"
#include "gfx/rendering/global_render_resource.h"
#include "resource/resource_manager.h"

namespace ngl::gfx
{

    // --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    bool StandardRenderModel::Initialize(rhi::DeviceDep* p_device, res::ResourceHandle<ResMeshData> res_mesh, std::shared_ptr<gfx::MeshData> override_mesh_shape_data, const char* material_name)
    {
        auto& res_manager = ngl::res::ResourceManager::Instance();

        res_mesh_ = res_mesh;

        override_mesh_shape_data_ = override_mesh_shape_data;

        material_array_ = {};
        material_array_.resize(res_mesh_->material_data_array_.size());
        for (int i = 0; i < material_array_.size(); ++i)
        {
            ResTexture::LoadDesc load_desc = {};

            if (0 < res_mesh_->material_data_array_[i].tex_basecolor.Length())
                material_array_[i].tex_basecolor = res_manager.LoadResource<ResTexture>(p_device, res_mesh_->material_data_array_[i].tex_basecolor.Get(), &load_desc);

            if (0 < res_mesh_->material_data_array_[i].tex_normal.Length())
                material_array_[i].tex_normal = res_manager.LoadResource<ResTexture>(p_device, res_mesh_->material_data_array_[i].tex_normal.Get(), &load_desc);

            if (0 < res_mesh_->material_data_array_[i].tex_occlusion.Length())
                material_array_[i].tex_occlusion = res_manager.LoadResource<ResTexture>(p_device, res_mesh_->material_data_array_[i].tex_occlusion.Get(), &load_desc);

            if (0 < res_mesh_->material_data_array_[i].tex_roughness.Length())
                material_array_[i].tex_roughness = res_manager.LoadResource<ResTexture>(p_device, res_mesh_->material_data_array_[i].tex_roughness.Get(), &load_desc);

            if (0 < res_mesh_->material_data_array_[i].tex_metalness.Length())
                material_array_[i].tex_metalness = res_manager.LoadResource<ResTexture>(p_device, res_mesh_->material_data_array_[i].tex_metalness.Get(), &load_desc);
        }

        // 標準不透明マテリアルでShape毎のマテリアルPsoを準備.
        // material_name = "opaque_standard";
        auto* shape_array = &(res_mesh_->data_.shape_array_);
        if(override_mesh_shape_data_)
        {
            // OverrideMeshShapeDataがある場合はそちらを参照.
            shape_array = &(override_mesh_shape_data_->shape_array_);
        }
        for (int i = 0; i < shape_array->size(); ++i)
        {
            shape_mtl_pso_set_.push_back(MaterialShaderManager::Instance().GetMaterialPsoSet(material_name, (*shape_array)[i].vtx_attr_mask_));
        }

        return true;
    }

    void StandardRenderModel::BindModelResourceCallback(BindModelResourceOptionCallbackArg arg)
    {
        auto default_white_tex_srv  = GlobalRenderResource::Instance().default_resource_.tex_white->ref_view_;
        auto default_black_tex_srv  = GlobalRenderResource::Instance().default_resource_.tex_black->ref_view_;
        auto default_normal_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_default_normal->ref_view_;

        const auto& shape_mat_index = res_mesh_->shape_material_index_array_[arg.shape_index];
        const auto& mat_data        = material_array_[shape_mat_index];

        arg.pso->SetView(arg.desc_set, "samp_default", GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
        // テクスチャ設定テスト. このあたりはDescriptorSetDepに事前にセットしておきたい.
        {
            auto tex_basecolor = (mat_data.tex_basecolor.IsValid()) ? mat_data.tex_basecolor->ref_view_ : default_white_tex_srv;
            auto tex_normal    = (mat_data.tex_normal.IsValid()) ? mat_data.tex_normal->ref_view_ : default_normal_tex_srv;
            auto tex_occlusion = (mat_data.tex_occlusion.IsValid()) ? mat_data.tex_occlusion->ref_view_ : default_white_tex_srv;
            auto tex_roughness = (mat_data.tex_roughness.IsValid()) ? mat_data.tex_roughness->ref_view_ : default_white_tex_srv;
            auto tex_metalness = (mat_data.tex_metalness.IsValid()) ? mat_data.tex_metalness->ref_view_ : default_black_tex_srv;

            arg.pso->SetView(arg.desc_set, "tex_basecolor", tex_basecolor.Get());
            arg.pso->SetView(arg.desc_set, "tex_occlusion", tex_occlusion.Get());
            arg.pso->SetView(arg.desc_set, "tex_normal", tex_normal.Get());
            arg.pso->SetView(arg.desc_set, "tex_roughness", tex_roughness.Get());
            arg.pso->SetView(arg.desc_set, "tex_metalness", tex_metalness.Get());
        }

        if (bind_model_resource_option_callback_)
        {
            // モデル固有のリソース設定コールバックが設定されている場合はそちらを呼び出す.
            bind_model_resource_option_callback_(arg);
        }
    }
    void StandardRenderModel::DrawShape(rhi::GraphicsCommandListDep* p_command_list, int shape_index)
    {
        if (draw_shape_override_)
        {
            // プロシージャル描画関数が設定されている場合はそちらを呼び出す.
            _DrawShapeOverrideFuncionArg draw_shape_override_arg;
            {
                draw_shape_override_arg.command_list = p_command_list;
                draw_shape_override_arg.shape_index  = shape_index;
            };
            draw_shape_override_(draw_shape_override_arg);
            return;
        }

        // Shape描画.
        
        auto* shape_array = &(res_mesh_->data_.shape_array_);
        if(override_mesh_shape_data_)
        {
            // OverrideMeshShapeDataがある場合はそちらを参照.
            shape_array = &(override_mesh_shape_data_->shape_array_);
        }
        if (shape_index >= shape_array->size())
        {
            return;
        }

        const gfx::MeshShapePart* shape = &(*shape_array)[shape_index];

        // 一括設定. Mesh描画はセマンティクスとスロットを固定化しているため, Meshデータロード時にマッピングを構築してそのまま利用する.
        // PSO側のInputLayoutが要求するセマンティクスとのValidationチェックも可能なはず.
        D3D12_VERTEX_BUFFER_VIEW vtx_views[gfx::MeshVertexSemantic::SemanticSlotMaxCount()] = {};
        for (auto vi = 0; vi < gfx::MeshVertexSemantic::SemanticSlotMaxCount(); ++vi)
        {
            if (shape->vtx_attr_mask_.mask & (1 << vi))
                vtx_views[vi] = shape->p_vtx_attr_mapping_[vi]->rhi_vbv_.GetView();
        }
        p_command_list->SetVertexBuffers(0, (u32)std::size(vtx_views), vtx_views);

        // Set Index and topology.
        p_command_list->SetIndexBuffer(&shape->index_.rhi_vbv_.GetView());
        p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);

        // Draw.
        p_command_list->DrawIndexedInstanced(shape->num_primitive_ * 3, 1, 0, 0, 0);
    }

}  // namespace ngl::gfx

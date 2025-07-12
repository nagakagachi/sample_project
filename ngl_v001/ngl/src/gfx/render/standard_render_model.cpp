
#include "gfx/render/standard_render_model.h"

#include "gfx/material/material_shader_manager.h"
#include "resource/resource_manager.h"

namespace ngl::gfx
{

    // --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    bool StandardRenderModel::Initialize(rhi::DeviceDep* p_device, res::ResourceHandle<ResMeshData> res_mesh, const char* material_name)
    {
        auto& res_manager = ngl::res::ResourceManager::Instance();

        res_mesh_ = res_mesh;

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
        for (int i = 0; i < res_mesh_->data_.shape_array_.size(); ++i)
        {
            shape_mtl_pso_set_.push_back(MaterialShaderManager::Instance().GetMaterialPsoSet(material_name, res_mesh_->data_.shape_array_[i].vtx_attr_mask_));
        }

        return true;
    }

    void StandardRenderModel::DrawShape(rhi::GraphicsCommandListDep* p_command_list, int shape_index)
    {
        if(procedural_draw_shape_func_)
        {
            // プロシージャル描画関数が設定されている場合はそちらを呼び出す.
            procedural_draw_shape_func_(p_command_list, shape_index);
            return;
        }

        // 規程のShape描画.
        auto& shape = res_mesh_->data_.shape_array_[shape_index];

        // 一括設定. Mesh描画はセマンティクスとスロットを固定化しているため, Meshデータロード時にマッピングを構築してそのまま利用する.
        // PSO側のInputLayoutが要求するセマンティクスとのValidationチェックも可能なはず.
        D3D12_VERTEX_BUFFER_VIEW vtx_views[gfx::MeshVertexSemantic::SemanticSlotMaxCount()] = {};
        for (auto vi = 0; vi < gfx::MeshVertexSemantic::SemanticSlotMaxCount(); ++vi)
        {
            if (shape.vtx_attr_mask_.mask & (1 << vi))
                vtx_views[vi] = shape.p_vtx_attr_mapping_[vi]->rhi_vbv_.GetView();
        }
        p_command_list->SetVertexBuffers(0, (u32)std::size(vtx_views), vtx_views);

        // Set Index and topology.
        p_command_list->SetIndexBuffer(&shape.index_.rhi_vbv_.GetView());
        p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);

        // Draw.
        p_command_list->DrawIndexedInstanced(shape.num_primitive_ * 3, 1, 0, 0, 0);
    }

}  // namespace ngl::gfx

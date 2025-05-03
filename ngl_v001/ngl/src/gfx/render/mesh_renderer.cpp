#pragma once

#include "gfx/render/mesh_renderer.h"

#include "gfx/render/global_render_resource.h"
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"

#include "gfx/common_struct.h"
#include "gfx/material/material_shader_manager.h"

namespace ngl
{
namespace gfx
{
	void RenderMeshWithMaterial(rhi::GraphicsCommandListDep& command_list
		, const char* pass_name, const std::vector<gfx::StaticMeshComponent*>& mesh_instance_array, const RenderMeshResource& render_mesh_resouce)
	{
    	auto default_white_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_white->ref_view_;
    	auto default_black_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_black->ref_view_;
    	auto default_normal_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_default_normal->ref_view_;
		
		for (int mesh_comp_i = 0; mesh_comp_i < mesh_instance_array.size(); ++mesh_comp_i)
		{
			const auto* e = mesh_instance_array[mesh_comp_i];

			auto cbv_instance = e->GetInstanceBufferView();


			for (int shape_i = 0; shape_i < e->model_.res_mesh_->data_.shape_array_.size(); ++shape_i)
			{
				// Geometry.
				auto& shape = e->model_.res_mesh_->data_.shape_array_[shape_i];
				const auto& shape_mat_index = e->model_.res_mesh_->shape_material_index_array_[shape_i];
				const auto& mat_data = e->model_.material_array_[shape_mat_index];

				// Shapeに対応したMaterial Pass Psoを取得.
				const auto&& pso = e->model_.shape_mtl_pso_set_[shape_i].GetPassPso(pass_name);
				command_list.SetPipelineState(pso);
				
				// Descriptor.
				{
					ngl::rhi::DescriptorSetDep desc_set;

					{
						if(auto* p_view = render_mesh_resouce.cbv_sceneview.p_view)
							pso->SetView(&desc_set, render_mesh_resouce.cbv_sceneview.slot_name.Get(), p_view);
					
						if(auto* p_view = render_mesh_resouce.cbv_d_shadowview.p_view)
							pso->SetView(&desc_set, render_mesh_resouce.cbv_d_shadowview.slot_name.Get(), p_view);
					}
					
					pso->SetView(&desc_set, "ngl_cb_instance", cbv_instance.Get());

					pso->SetView(&desc_set, "samp_default", GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
					// テクスチャ設定テスト. このあたりはDescriptorSetDepに事前にセットしておきたい.
					{
						auto tex_basecolor = (mat_data.tex_basecolor.IsValid())? mat_data.tex_basecolor->ref_view_ : default_white_tex_srv;
						auto tex_normal = (mat_data.tex_normal.IsValid())? mat_data.tex_normal->ref_view_ : default_normal_tex_srv;
						auto tex_occlusion = (mat_data.tex_occlusion.IsValid())? mat_data.tex_occlusion->ref_view_ : default_white_tex_srv;
						auto tex_roughness = (mat_data.tex_roughness.IsValid())? mat_data.tex_roughness->ref_view_ : default_white_tex_srv;
						auto tex_metalness = (mat_data.tex_metalness.IsValid())? mat_data.tex_metalness->ref_view_ : default_black_tex_srv;

						#if 1
							pso->SetView(&desc_set, "tex_basecolor", tex_basecolor.Get());
							pso->SetView(&desc_set, "tex_occlusion", tex_occlusion.Get());
							pso->SetView(&desc_set, "tex_normal", tex_normal.Get());
							pso->SetView(&desc_set, "tex_roughness", tex_roughness.Get());
							pso->SetView(&desc_set, "tex_metalness", tex_metalness.Get());
						#else
							// DescriptorSetへの設定時に歯抜けをデフォルトDescriptorで埋めておく最適化の確認用に逆順になりやすい順序で設定する. やっていることは↑と同じ.
							pso->SetView(&desc_set, "tex_metalness", tex_metalness.Get());
							pso->SetView(&desc_set, "tex_roughness", tex_roughness.Get());
							pso->SetView(&desc_set, "tex_occlusion", tex_occlusion.Get());
							pso->SetView(&desc_set, "tex_normal", tex_normal.Get());
							pso->SetView(&desc_set, "tex_basecolor", tex_basecolor.Get());
						#endif
					}

					// DescriptorSetでViewを設定.
					command_list.SetDescriptorSet(pso, &desc_set);
				}



				// 一括設定. Mesh描画はセマンティクスとスロットを固定化しているため, Meshデータロード時にマッピングを構築してそのまま利用する.
				// PSO側のInputLayoutが要求するセマンティクスとのValidationチェックも可能なはず.
				D3D12_VERTEX_BUFFER_VIEW vtx_views[gfx::MeshVertexSemantic::SemanticSlotMaxCount()] = {};
				for (auto vi = 0; vi < gfx::MeshVertexSemantic::SemanticSlotMaxCount(); ++vi)
				{
					if (shape.vtx_attr_mask_.mask & (1 << vi))
						vtx_views[vi] = shape.p_vtx_attr_mapping_[vi]->rhi_vbv_.GetView();
				}
				command_list.SetVertexBuffers(0, (u32)std::size(vtx_views), vtx_views);

				// Set Index and topology.
				command_list.SetIndexBuffer(&shape.index_.rhi_vbv_.GetView());
				command_list.SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);

				// Draw.
				command_list.DrawIndexedInstanced(shape.num_primitive_ * 3, 1, 0, 0, 0);
			}
		}
	}
	
}
}

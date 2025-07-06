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
	void RenderMeshWithMaterial(rhi::GraphicsCommandListDep& command_list,
		const char* pass_name, fwk::GfxScene* gfx_scene, const std::vector<fwk::GfxSceneEntityId>& mesh_proxy_id_array, const RenderMeshResource& render_mesh_resouce)
	{
    	auto default_white_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_white->ref_view_;
    	auto default_black_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_black->ref_view_;
    	auto default_normal_tex_srv = GlobalRenderResource::Instance().default_resource_.tex_default_normal->ref_view_;

		auto* mesh_proxy_buffer = gfx_scene->GetEntityProxyBuffer<fwk::GfxSceneEntityMesh>();
		for (int mesh_comp_i = 0; mesh_comp_i < mesh_proxy_id_array.size(); ++mesh_comp_i)
		{
			const auto proxy_id = mesh_proxy_id_array[mesh_comp_i];
			assert(fwk::GfxSceneEntityId::IsValid(proxy_id));
			
			auto* mesh_proxy = mesh_proxy_buffer->proxy_buffer_[proxy_id.GetIndex()];
			auto* model = mesh_proxy->model_;


			auto mesh_instance_cbh = command_list.GetDevice()->GetConstantBufferPool()->Alloc(sizeof(InstanceInfo));
			if (auto* map_ptr = mesh_instance_cbh->buffer_.MapAs<InstanceInfo>())
			{
				map_ptr->mtx = mesh_proxy->transform_;
				map_ptr->mtx_cofactor = math::Mat34(math::Mat33::Cofactor(mesh_proxy->transform_.GetMat33()));// 余因子行列.

				mesh_instance_cbh->buffer_.Unmap();
			}
			

			for (int shape_i = 0; shape_i < model->res_mesh_->data_.shape_array_.size(); ++shape_i)
			{
				const auto& shape_mat_index = model->res_mesh_->shape_material_index_array_[shape_i];
				const auto& mat_data = model->material_array_[shape_mat_index];

				// Shapeに対応したMaterial Pass Psoを取得.
				const auto&& pso = model->shape_mtl_pso_set_[shape_i].GetPassPso(pass_name);
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
					
					pso->SetView(&desc_set, "ngl_cb_instance", &mesh_instance_cbh->cbv_);

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

				// Geometry.
                model->DrawShape(&command_list, shape_i);
			}
		}
	}
	
}
}

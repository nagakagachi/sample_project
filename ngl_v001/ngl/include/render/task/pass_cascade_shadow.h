#pragma once

#include <thread>

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "gfx/material/material_shader_manager.h"
#include "gfx/rendering/mesh_renderer.h"

namespace ngl::render::task
{
    
	struct CascadeShadowMapParameter
	{
		static constexpr int k_cascade_count = 3;// シェーダ側と合わせる. 安全と思われる最大数.

		float split_distance_ws[k_cascade_count];
		float split_rate[k_cascade_count];
		
		int cascade_tile_offset_x[k_cascade_count];
		int cascade_tile_offset_y[k_cascade_count];
		int cascade_tile_size_x[k_cascade_count];
		int cascade_tile_size_y[k_cascade_count];
		
		int atlas_resolution;
		
		math::Mat34 light_view_mtx[k_cascade_count];
		math::Mat44 light_ortho_mtx[k_cascade_count];
	};

	
	// Directional Cascade Shadow Rendering用 定数バッファ構造定義.
	struct SceneDirectionalShadowRenderInfo
	{
		math::Mat34 cb_shadow_view_mtx;
		math::Mat34 cb_shadow_view_inv_mtx;
		math::Mat44 cb_shadow_proj_mtx;
		math::Mat44 cb_shadow_proj_inv_mtx;
	};
	
	// DirectionalShadow Sampling用.
	struct SceneDirectionalShadowSampleInfo
	{
		static constexpr int k_directional_shadow_cascade_cb_max = 8;
		
		math::Mat34 cb_shadow_view_mtx[k_directional_shadow_cascade_cb_max];
		math::Mat34 cb_shadow_view_inv_mtx[k_directional_shadow_cascade_cb_max];
		math::Mat44 cb_shadow_proj_mtx[k_directional_shadow_cascade_cb_max];
		math::Mat44 cb_shadow_proj_inv_mtx[k_directional_shadow_cascade_cb_max];
		
		math::Vec4 cb_cascade_tile_uvoffset_uvscale[k_directional_shadow_cascade_cb_max];
		
		// 各Cascadeの遠方側境界のView距離. 格納はアライメント対策で4要素ずつ.
		math::Vec4 cb_cascade_far_distance4[k_directional_shadow_cascade_cb_max/4];
		
		int cb_valid_cascade_count;// float配列の後ろだとCBアライメントでずれるのでここに.
	};
	
	
	// DirectionalShadowパス.
	struct TaskDirectionalShadowPass : public rtg::IGraphicsTaskNode
	{
		rtg::RtgResourceHandle h_shadow_depth_atlas_{};

		rhi::ConstantBufferPooledHandle	shadow_sample_cbh_{};
		// Cascade情報. Setupで計算.
		CascadeShadowMapParameter csm_param_{};

		struct SetupDesc
		{
			rhi::ConstantBufferPooledHandle scene_cbv{};
			
			fwk::GfxScene* gfx_scene{};
			const std::vector<fwk::GfxSceneEntityId>* p_mesh_proxy_id_array_{};

			math::Vec3 directional_light_dir{};

			bool		dbg_per_cascade_multithread = true;
		};
		SetupDesc desc_{};
		bool is_render_skip_debug{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
			auto* p_cb_pool = p_device->GetConstantBufferPool();

			desc_ = desc;
			
			// Cascade Shadowの最遠方距離.
			const float shadowmap_far_range = 160.0f;
			// Cascade Shadowの最近接Cascadeのカバー距離.
			const float shadowmap_nearest_cascade_range = 12.0f;
			// Cascade Shadowの最近接より遠方のCascadeの分割用指数.
			const float shadowmap_cascade_split_power = 2.4f;
			// Cascade間ブレンド幅.
			const float k_cascade_blend_width_ws = 5.0f;
			// Cascade 1つのサイズ.
			constexpr int shadowmap_single_reso = 1024*2;
			// CascadeをAtlas管理する際のトータルサイズ.
			constexpr int shadowmap_atlas_reso = shadowmap_single_reso * 2;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				rtg::RtgResourceDesc2D depth_desc =
					rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(shadowmap_atlas_reso, shadowmap_atlas_reso, gfx::MaterialPassPsoCreator_depth::k_depth_format);

				// リソースアクセス定義.
				h_shadow_depth_atlas_ = builder.RecordResourceAccess(*this, builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);
			}
			
			// ShadowSample用の定数バッファ.
			shadow_sample_cbh_ = p_cb_pool->Alloc(sizeof(SceneDirectionalShadowSampleInfo));

			// ----------------------------------
			// Cascade情報セットアップ.
			csm_param_ = {};
			csm_param_.atlas_resolution = shadowmap_atlas_reso;
			
			const auto view_forward_dir = view_info.camera_pose.GetColumn2();
			const auto view_up_dir = view_info.camera_pose.GetColumn1();
			const auto view_right_dir = view_info.camera_pose.GetColumn0();// LeftHandとしてSideAxis(右)ベクトル.

			// 単位Frustumの頂点へのベクトル.
			math::ViewPositionRelativeFrustumCorners frustum_corners;
			math::CreateFrustumCorners(frustum_corners,
				view_info.camera_pos, view_forward_dir, view_up_dir, view_right_dir, view_info.camera_fov_y, view_info.aspect_ratio);

			// 最近接Cascade距離を安全のためクランプ. 等間隔分割の場合を上限とする.
			const float first_cascade_distance = std::min(shadowmap_nearest_cascade_range, shadowmap_far_range / static_cast<float>(csm_param_.k_cascade_count));
			const float first_cascade_split_rate = first_cascade_distance / shadowmap_far_range;
			for(int ci = 0; ci < csm_param_.k_cascade_count; ++ci)
			{
				const float cascade_rate = static_cast<float>(ci) / static_cast<float>(csm_param_.k_cascade_count - 1);
				// 最近接Cascadeより後ろは指数分割.
				const float remain_rate_term = (1.0f - first_cascade_split_rate) * std::powf(cascade_rate, shadowmap_cascade_split_power);
				const float split_rate = first_cascade_split_rate + remain_rate_term;
				
				// 分割位置割合とワールド距離.
				csm_param_.split_rate[ci] = split_rate;
				csm_param_.split_distance_ws[ci] = split_rate * shadowmap_far_range;
			}
			const math::Vec3 lightview_forward = desc.directional_light_dir;
			const math::Vec3 lightview_helper_side_unit = (0.9f > std::abs(lightview_forward.x))? math::Vec3::UnitX() : math::Vec3::UnitZ();
			const math::Vec3 lightview_up = math::Vec3::Normalize(math::Vec3::Cross(lightview_forward, lightview_helper_side_unit));
			const math::Vec3 lightview_side = math::Vec3::Normalize(math::Vec3::Cross(lightview_up, lightview_forward));

			// 原点位置のLitght ViewMtx.
			const math::Mat34 lightview_view_mtx = math::CalcViewMatrix(math::Vec3::Zero(), lightview_forward, lightview_up);

			
			for(int ci = 0; ci < csm_param_.k_cascade_count; ++ci)
			{
				// frustum_corners を利用して分割面の位置計算. Cascade間ブレンド用にnear側を拡張.
				const float near_dist_ws = (0 == ci)? view_info.near_z : std::max(0.0f, csm_param_.split_distance_ws[ci-1] - k_cascade_blend_width_ws);
				const float far_dist_ws = csm_param_.split_distance_ws[ci];

				math::Vec3 split_frustum_near4_far4_ws[] =
				{
					frustum_corners.corner_vec[0] * near_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[1] * near_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[2] * near_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[3] * near_dist_ws + frustum_corners.view_pos,
					
					frustum_corners.corner_vec[0] * far_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[1] * far_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[2] * far_dist_ws + frustum_corners.view_pos,
					frustum_corners.corner_vec[3] * far_dist_ws + frustum_corners.view_pos,
				};

				// LightViewでのAABBからOrthoを計算しようとしている.
				//	しかしViewDistanceとLightViewAABBの範囲がうまく重ならずCascade境界が空白になる問題.
				{
					math::Vec3 lightview_vtx_pos_min = math::Vec3(FLT_MAX);
					math::Vec3 lightview_vtx_pos_max = math::Vec3(-FLT_MAX);
					for(int fvi = 0; fvi < std::size(split_frustum_near4_far4_ws); ++fvi)
					{
						const math::Vec3 lightview_vtx_pos(
							math::Vec3::Dot(split_frustum_near4_far4_ws[fvi], lightview_side),
							math::Vec3::Dot(split_frustum_near4_far4_ws[fvi], lightview_up),
							math::Vec3::Dot(split_frustum_near4_far4_ws[fvi], lightview_forward)
						);

						lightview_vtx_pos_min.x = std::min(lightview_vtx_pos_min.x, lightview_vtx_pos.x);
						lightview_vtx_pos_min.y = std::min(lightview_vtx_pos_min.y, lightview_vtx_pos.y);
						lightview_vtx_pos_min.z = std::min(lightview_vtx_pos_min.z, lightview_vtx_pos.z);
						
						lightview_vtx_pos_max.x = std::max(lightview_vtx_pos_max.x, lightview_vtx_pos.x);
						lightview_vtx_pos_max.y = std::max(lightview_vtx_pos_max.y, lightview_vtx_pos.y);
						lightview_vtx_pos_max.z = std::max(lightview_vtx_pos_max.z, lightview_vtx_pos.z);
					}
					
					constexpr float shadow_near_far_offset = 200.0f;
					const math::Mat44 lightview_ortho = math::CalcReverseOrthographicMatrix(
						lightview_vtx_pos_min.x, lightview_vtx_pos_max.x,
						lightview_vtx_pos_min.y, lightview_vtx_pos_max.y,
						lightview_vtx_pos_min.z - shadow_near_far_offset, lightview_vtx_pos_max.z + shadow_near_far_offset
						);

					csm_param_.light_view_mtx[ci] = lightview_view_mtx;
					csm_param_.light_ortho_mtx[ci] = lightview_ortho;
				}
			}

			for(int ci = 0; ci < csm_param_.k_cascade_count; ++ci)
			{
				// 2x2 Atlas のTileのマッピングを決定.
				assert((2*2) > csm_param_.k_cascade_count);// 現状は 2x2 のAtlas固定で各所の実装をしているのでチェック.
				const int cascade_tile_x = ci & 0x01;
				const int cascade_tile_y = (ci >> 1) & 0x01;
				csm_param_.cascade_tile_offset_x[ci] = shadowmap_single_reso * cascade_tile_x;
				csm_param_.cascade_tile_offset_y[ci] = shadowmap_single_reso * cascade_tile_y;
				csm_param_.cascade_tile_size_x[ci] = shadowmap_single_reso;
				csm_param_.cascade_tile_size_y[ci] = shadowmap_single_reso;
			}

			if(auto* mapped = shadow_sample_cbh_->buffer_.MapAs<SceneDirectionalShadowSampleInfo>())
			{
				assert(csm_param_.k_cascade_count < mapped->k_directional_shadow_cascade_cb_max);// バッファサイズが足りているか.

				mapped->cb_valid_cascade_count = csm_param_.k_cascade_count;
				
				for(int ci = 0; ci < csm_param_.k_cascade_count; ++ci)
				{
					mapped->cb_shadow_view_mtx[ci] = csm_param_.light_view_mtx[ci];
					mapped->cb_shadow_proj_mtx[ci] = csm_param_.light_ortho_mtx[ci];
					mapped->cb_shadow_view_inv_mtx[ci] = ngl::math::Mat34::Inverse(csm_param_.light_view_mtx[ci]);
					mapped->cb_shadow_proj_inv_mtx[ci] = ngl::math::Mat44::Inverse(csm_param_.light_ortho_mtx[ci]);

					// Sample用のAtlas上のUV情報.
					const auto tile_offset_x_f = static_cast<float>(csm_param_.cascade_tile_offset_x[ci]);
					const auto tile_offset_y_f = static_cast<float>(csm_param_.cascade_tile_offset_y[ci]);
					const auto tile_size_x_f = static_cast<float>(csm_param_.cascade_tile_size_x[ci]);
					const auto tile_size_y_f = static_cast<float>(csm_param_.cascade_tile_size_y[ci]);
					const auto atlas_size_f = static_cast<float>(csm_param_.atlas_resolution);
					mapped->cb_cascade_tile_uvoffset_uvscale[ci] =
						math::Vec4(tile_offset_x_f, tile_offset_y_f, tile_size_x_f, tile_size_y_f) / atlas_size_f;
					
					mapped->cb_cascade_far_distance4[ci/4].data[ci%4] = csm_param_.split_distance_ws[ci];
				}
				
				shadow_sample_cbh_->buffer_.Unmap();
			}
			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					if(is_render_skip_debug)
					{
						return;
					}

					if(desc_.dbg_per_cascade_multithread)
					{
						// Cascade毎にマルチスレッド実行するためにCommandListを確保.
						command_list_allocator.Alloc(csm_param_.k_cascade_count);
					}
					else
					{
						// シングルスレッド実行なので1つだけCommandList確保.
						command_list_allocator.Alloc(1);
					}
					
					// マルチスレッドで複数CommandListを使うためScopedMakerでは対応できない. 直接適切なCommandListにMarkerをPushする.
					command_list_allocator.GetOrCreate_Front()->BeginMarker("Shadow");
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_shadow_depth_atlas = builder.GetAllocatedResource(this, h_shadow_depth_atlas_);
					assert(res_shadow_depth_atlas.tex_.IsValid() && res_shadow_depth_atlas.dsv_.IsValid());

					// Atlas全域クリア.
					command_list_allocator.GetOrCreate_Front()->ClearDepthTarget(res_shadow_depth_atlas.dsv_.Get(), 0.0f, 0, true, true);// とりあえずクリアだけ.ReverseZなので0クリア.

					// Cascade単位のレンダリング.
					auto render_per_cascade = [this, res_shadow_depth_atlas](int cascade_index, rhi::GraphicsCommandListDep* command_list, const rtg::RtgAllocatedResourceInfo& shadow_atlas)
					{
						auto* thread_command_list = command_list;
						
						NGL_RHI_GPU_SCOPED_EVENT_MARKER(thread_command_list, text::FixedString<64>("Cascade_%d", cascade_index));

						// D3D ValidationErrorになるため, CommandList毎に同じTarget設定コマンドを発行している.
						thread_command_list->SetRenderTargets(nullptr, 0, shadow_atlas.dsv_.Get());
							
						// Cascade用の定数バッファを都度生成.
						auto shadow_cb_h = thread_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(SceneDirectionalShadowRenderInfo));
						if (auto* mapped = shadow_cb_h->buffer_.MapAs<SceneDirectionalShadowRenderInfo>())
						{
							const auto csm_info_index = cascade_index;
								
							assert(csm_info_index < csm_param_.k_cascade_count);// 不正なIndexでないか.
							mapped->cb_shadow_view_mtx = csm_param_.light_view_mtx[csm_info_index];
							mapped->cb_shadow_proj_mtx = csm_param_.light_ortho_mtx[csm_info_index];
							mapped->cb_shadow_view_inv_mtx = ngl::math::Mat34::Inverse(csm_param_.light_view_mtx[csm_info_index]);
							mapped->cb_shadow_proj_inv_mtx = ngl::math::Mat44::Inverse(csm_param_.light_ortho_mtx[csm_info_index]);
							
							shadow_cb_h->buffer_.Unmap();
						}

						const auto cascade_tile_w = csm_param_.cascade_tile_size_x[cascade_index];
						const auto cascade_tile_h = csm_param_.cascade_tile_size_y[cascade_index];
						const auto cascade_tile_offset_x = csm_param_.cascade_tile_offset_x[cascade_index];
						const auto cascade_tile_offset_y = csm_param_.cascade_tile_offset_y[cascade_index];
						ngl::gfx::helper::SetFullscreenViewportAndScissor(thread_command_list, cascade_tile_offset_x, cascade_tile_offset_y, cascade_tile_w, cascade_tile_h);

						// Mesh Rendering.
						gfx::RenderMeshResource render_mesh_res = {};
						{
							render_mesh_res.cbv_sceneview = {"ngl_cb_sceneview", &desc_.scene_cbv->cbv_};
							render_mesh_res.cbv_d_shadowview = {"ngl_cb_shadowview", &shadow_cb_h->cbv_};
						}

						ngl::gfx::RenderMeshWithMaterial(*thread_command_list, gfx::MaterialPassPsoCreator_d_shadow::k_name, desc_.gfx_scene, *desc_.p_mesh_proxy_id_array_, render_mesh_res);
					};

					if(desc_.dbg_per_cascade_multithread)
					{
						// Cascade毎にマルチスレッド実行.
					
						// シンプルにstd::thread使用. 理想的には起動済みのJobSystemスレッドを利用したい.
						// 0番以外を別スレッド実行.
						std::vector<std::thread*> thread_array;
						for(int cascade_index = 1; cascade_index < csm_param_.k_cascade_count; ++cascade_index)
						{
							thread_array.push_back( new std::thread( render_per_cascade, cascade_index, command_list_allocator.GetOrCreate(cascade_index), res_shadow_depth_atlas));
						}
						// 0番はカレントスレッドで実行.
						constexpr  int cascade_index0 = 0;
						std::invoke(render_per_cascade, cascade_index0, command_list_allocator.GetOrCreate(cascade_index0), res_shadow_depth_atlas);
					
						// thread完了待ち.
						for(auto&& t : thread_array)
						{
							t->join();
						}
					}
					else
					{
						// シングルスレッド実行.
						
						for(int cascade_index = 0; cascade_index < csm_param_.k_cascade_count; ++cascade_index)
						{
							// 先頭CommandListにすべてのレンダリングを実行.
							render_per_cascade(cascade_index, command_list_allocator.GetOrCreate_Front(), res_shadow_depth_atlas);
						}
					}

					// 最終CommandListにMarker終了をPush.
					command_list_allocator.GetOrCreate_Back()->EndMarker();
				});
		}
	};
}
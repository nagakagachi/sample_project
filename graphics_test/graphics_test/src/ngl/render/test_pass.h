#pragma once


#include <numeric>
#include <valarray>
#include<variant>

#include "ngl/gfx/rtg/graph_builder.h"
#include "ngl/gfx/command_helper.h"
#include "ngl/gfx/render/mesh_renderer.h"
#include "ngl/gfx/raytrace_scene.h"
#include "ngl/gfx/render/global_render_resource.h"

#include "ngl/gfx/material/material_shader_manager.h"

#include "ngl/util/time/timer.h"

namespace ngl::render
{

	namespace task
	{
		static constexpr char k_shader_model[] = "6_3";

		// Pass用のView情報.
		struct RenderPassViewInfo
		{
			ngl::math::Vec3		camera_pos = {};
			ngl::math::Mat33	camera_pose = ngl::math::Mat33::Identity();
			float				near_z{};
			float				far_z{};
			float				aspect_ratio{};
			float				camera_fov_y = ngl::math::Deg2Rad(60.0f);
		};
		
		// PreZパス.
		struct TaskDepthPass : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_depth_{};

			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};
				const std::vector<gfx::StaticMeshComponent*>* p_mesh_list{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info, const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					rtg::RtgResourceDesc2D depth_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, gfx::MaterialPassPsoCreator_depth::k_depth_format);
					h_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "DepthPass");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_depth = builder.GetAllocatedResource(this, h_depth_);
				assert(res_depth.tex_.IsValid() && res_depth.dsv_.IsValid());

				gfx_commandlist->ClearDepthTarget(res_depth.dsv_.Get(), 0.0f, 0, true, true);// とりあえずクリアだけ.ReverseZなので0クリア.
				// Set RenderTarget.
				gfx_commandlist->SetRenderTargets(nullptr, 0, res_depth.dsv_.Get());
				// Set Viewport and Scissor.
				ngl::gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_depth.tex_->GetWidth(), res_depth.tex_->GetHeight());
				
				// Mesh Rendering.
				gfx::RenderMeshResource render_mesh_res = {};
				{
					render_mesh_res.cbv_sceneview = {"ngl_cb_sceneview", desc_.ref_scene_cbv.Get()};
				}
				ngl::gfx::RenderMeshWithMaterial(*gfx_commandlist, gfx::MaterialPassPsoCreator_depth::k_name, *desc_.p_mesh_list, render_mesh_res);
			}
		};


		// GBufferパス.
		struct TaskGBufferPass : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_depth_{};
			rtg::RtgResourceHandle h_gb0_{};
			rtg::RtgResourceHandle h_gb1_{};
			rtg::RtgResourceHandle h_gb2_{};
			rtg::RtgResourceHandle h_gb3_{};
			rtg::RtgResourceHandle h_velocity_{};
			
			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};
				const std::vector<gfx::StaticMeshComponent*>* p_mesh_list{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_async_write_tex,
				const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					// MaterialPassに合わせてFormat設定.
					constexpr auto k_gbuffer0_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer0_format;
					constexpr auto k_gbuffer1_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer1_format;
					constexpr auto k_gbuffer2_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer2_format;
					constexpr auto k_gbuffer3_format = gfx::MaterialPassPsoCreator_gbuffer::k_gbuffer3_format;
					constexpr auto k_velocity_format = gfx::MaterialPassPsoCreator_gbuffer::k_velocity_format;
					
					// GBuffer0 BaseColor.xyz, Occlusion.w
					rtg::RtgResourceDesc2D gbuffer0_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer0_format);
					// GBuffer1 WorldNormal.xyz, 1bitOption.w
					rtg::RtgResourceDesc2D gbuffer1_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer1_format);
					// GBuffer2 Roughness, Metallic, Optional, MaterialId
					rtg::RtgResourceDesc2D gbuffer2_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer2_format);
					// GBuffer3 Emissive.xyz, Unused.w
					rtg::RtgResourceDesc2D gbuffer3_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_gbuffer3_format);
					// Velocity xy
					rtg::RtgResourceDesc2D velocity_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, k_velocity_format);

					// DepthのFormat取得.
					const auto depth_desc = builder.GetResourceHandleDesc(h_depth);
				
					// リソースアクセス定義.
					if(!h_async_write_tex.IsInvalid())
					{
						// 試しにAsyncComputeで書き込まれたリソースを読み取り.
						builder.RecordResourceAccess(*this, h_async_write_tex, rtg::access_type::SHADER_READ);
					}
				
					h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::DEPTH_TARGET);
					h_gb0_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer0_desc), rtg::access_type::RENDER_TARTGET);
					h_gb1_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer1_desc), rtg::access_type::RENDER_TARTGET);
					h_gb2_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer2_desc), rtg::access_type::RENDER_TARTGET);
					h_gb3_ = builder.RecordResourceAccess(*this, builder.CreateResource(gbuffer3_desc), rtg::access_type::RENDER_TARTGET);
					h_velocity_ = builder.RecordResourceAccess(*this, builder.CreateResource(velocity_desc), rtg::access_type::RENDER_TARTGET);
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "GBuffer");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_depth = builder.GetAllocatedResource(this, h_depth_);
				auto res_gb0 = builder.GetAllocatedResource(this, h_gb0_);
				auto res_gb1 = builder.GetAllocatedResource(this, h_gb1_);
				auto res_gb2 = builder.GetAllocatedResource(this, h_gb2_);
				auto res_gb3 = builder.GetAllocatedResource(this, h_gb3_);
				auto res_velocity = builder.GetAllocatedResource(this, h_velocity_);

				assert(res_depth.tex_.IsValid() && res_depth.dsv_.IsValid());
				assert(res_gb0.tex_.IsValid() && res_gb0.rtv_.IsValid());
				assert(res_gb1.tex_.IsValid() && res_gb1.rtv_.IsValid());
				assert(res_gb2.tex_.IsValid() && res_gb2.rtv_.IsValid());
				assert(res_gb3.tex_.IsValid() && res_gb3.rtv_.IsValid());
				assert(res_velocity.tex_.IsValid() && res_velocity.rtv_.IsValid());

				const rhi::RenderTargetViewDep* p_targets[] =
				{
					res_gb0.rtv_.Get(),
					res_gb1.rtv_.Get(),
					res_gb2.rtv_.Get(),
					res_gb3.rtv_.Get(),
					res_velocity.rtv_.Get(),
				};
				
				// Set RenderTarget.
				gfx_commandlist->SetRenderTargets(p_targets, (int)std::size(p_targets), res_depth.dsv_.Get());
				// Set Viewport and Scissor.
				ngl::gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_depth.tex_->GetWidth(), res_depth.tex_->GetHeight());

				// Mesh Rendering.
				gfx::RenderMeshResource render_mesh_res = {};
				{
					render_mesh_res.cbv_sceneview = {"ngl_cb_sceneview", desc_.ref_scene_cbv.Get()};
				}
				ngl::gfx::RenderMeshWithMaterial(*gfx_commandlist, gfx::MaterialPassPsoCreator_gbuffer::k_name, *desc_.p_mesh_list, render_mesh_res);
			}
		};

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


			rhi::RefBufferDep ref_d_shadow_sample_cb_{};
			rhi::RefCbvDep ref_d_shadow_sample_cbv_{};// 内部用.
			// Cascade情報. Setupで計算.
			CascadeShadowMapParameter csm_param_{};

			struct SetupDesc
			{
				rhi::RefCbvDep ref_scene_cbv{};
				const std::vector<gfx::StaticMeshComponent*>* p_mesh_list{};

				math::Vec3 directional_light_dir{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				const SetupDesc& desc)
			{
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
				ref_d_shadow_sample_cb_ = new rhi::BufferDep();
				{
					rhi::BufferDep::Desc cb_desc{};
					cb_desc.SetupAsConstantBuffer(sizeof(SceneDirectionalShadowSampleInfo));
					ref_d_shadow_sample_cb_->Initialize(p_device, cb_desc);
				}
				ref_d_shadow_sample_cbv_ = new rhi::ConstantBufferViewDep();
				{
					rhi::ConstantBufferViewDep::Desc cbv_desc{};
					ref_d_shadow_sample_cbv_->Initialize(ref_d_shadow_sample_cb_.Get(), cbv_desc);
				}

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

				if (auto* mapped = ref_d_shadow_sample_cb_->MapAs<SceneDirectionalShadowSampleInfo>())
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
					
					ref_d_shadow_sample_cb_->Unmap();
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "Shadow");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_shadow_depth_atlas = builder.GetAllocatedResource(this, h_shadow_depth_atlas_);
				assert(res_shadow_depth_atlas.tex_.IsValid() && res_shadow_depth_atlas.dsv_.IsValid());

				
				// Atlas全域クリア.
				gfx_commandlist->ClearDepthTarget(res_shadow_depth_atlas.dsv_.Get(), 0.0f, 0, true, true);// とりあえずクリアだけ.ReverseZなので0クリア.
				// Set RenderTarget.
				gfx_commandlist->SetRenderTargets(nullptr, 0, res_shadow_depth_atlas.dsv_.Get());
				
				// 描画するCascadeIndex.
				for(int cascade_index = 0; cascade_index < csm_param_.k_cascade_count; ++cascade_index)
				{
					NGL_SCOPED_EVENT_MARKER(gfx_commandlist, text::FixedString<64>("Cascade_%d", cascade_index));
					
					// Cascade用の定数バッファを都度生成.
					rhi::RefBufferDep ref_shadow_render_cb = new rhi::BufferDep();
					rhi::RefCbvDep ref_shadow_render_cbv = new rhi::ConstantBufferViewDep();
					{
						rhi::BufferDep::Desc cb_desc{};
						cb_desc.SetupAsConstantBuffer(sizeof(SceneDirectionalShadowRenderInfo));
						ref_shadow_render_cb->Initialize(gfx_commandlist->GetDevice(), cb_desc);
					}
					{
						rhi::ConstantBufferViewDep::Desc cbv_desc{};
						ref_shadow_render_cbv->Initialize(ref_shadow_render_cb.Get(), cbv_desc);
					}
					if (auto* mapped = ref_shadow_render_cb->MapAs<SceneDirectionalShadowRenderInfo>())
					{
						const auto csm_info_index = cascade_index;
						
						assert(csm_info_index < csm_param_.k_cascade_count);// 不正なIndexでないか.
						mapped->cb_shadow_view_mtx = csm_param_.light_view_mtx[csm_info_index];
						mapped->cb_shadow_proj_mtx = csm_param_.light_ortho_mtx[csm_info_index];
						mapped->cb_shadow_view_inv_mtx = ngl::math::Mat34::Inverse(csm_param_.light_view_mtx[csm_info_index]);
						mapped->cb_shadow_proj_inv_mtx = ngl::math::Mat44::Inverse(csm_param_.light_ortho_mtx[csm_info_index]);
					
						ref_d_shadow_sample_cb_->Unmap();
					}

					const auto cascade_tile_w = csm_param_.cascade_tile_size_x[cascade_index];
					const auto cascade_tile_h = csm_param_.cascade_tile_size_y[cascade_index];
					const auto cascade_tile_offset_x = csm_param_.cascade_tile_offset_x[cascade_index];
					const auto cascade_tile_offset_y = csm_param_.cascade_tile_offset_y[cascade_index];
					ngl::gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, cascade_tile_offset_x, cascade_tile_offset_y, cascade_tile_w, cascade_tile_h);

					// Mesh Rendering.
					gfx::RenderMeshResource render_mesh_res = {};
					{
						render_mesh_res.cbv_sceneview = {"ngl_cb_sceneview", desc_.ref_scene_cbv.Get()};
						render_mesh_res.cbv_d_shadowview = {"ngl_cb_shadowview", ref_shadow_render_cbv.Get()};
					}
					ngl::gfx::RenderMeshWithMaterial(*gfx_commandlist, gfx::MaterialPassPsoCreator_d_shadow::k_name, *desc_.p_mesh_list, render_mesh_res);
				}
			}
		};
		

		// LinearDepthパス.
		struct TaskLinearDepthPass : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_depth_{};
			rtg::RtgResourceHandle h_linear_depth_{};
;
			rhi::RhiRef<rhi::ComputePipelineStateDep> pso_;

			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_tex_compute, const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					// リソース定義.
					rtg::RtgResourceDesc2D linear_depth_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32_FLOAT);

					// Async Computeの出力リソースを読み取るテスト.
					if(!h_tex_compute.IsInvalid())
					{
						builder.RecordResourceAccess(*this, h_tex_compute, rtg::access_type::SHADER_READ);
					}
					
					// リソースアクセス定義.
					h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::SHADER_READ);
					h_linear_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(linear_depth_desc), rtg::access_type::UAV);
				}

				{
					auto& ResourceMan = ngl::res::ResourceManager::Instance();

					ngl::gfx::ResShader::LoadDesc loaddesc = {};
					loaddesc.entry_point_name = "main_cs";
					loaddesc.stage = ngl::rhi::EShaderStage::Compute;
					loaddesc.shader_model_version = k_shader_model;
					auto res_shader = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/screen/generate_lineardepth_cs.hlsl", &loaddesc);

					ngl::rhi::ComputePipelineStateDep::Desc pso_desc = {};
					pso_desc.cs = &res_shader->data_;
					pso_ = new rhi::ComputePipelineStateDep();
					if (!pso_->Initialize(p_device, pso_desc))
					{
						assert(false);
					}
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "LinearDepth");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_depth = builder.GetAllocatedResource(this, h_depth_);
				auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);

				assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
				assert(res_linear_depth.tex_.IsValid() && res_linear_depth.uav_.IsValid());

				ngl::rhi::DescriptorSetDep desc_set = {};
				pso_->SetView(&desc_set, "TexHardwareDepth", res_depth.srv_.Get());
				pso_->SetView(&desc_set, "RWTexLinearDepth", res_linear_depth.uav_.Get());
				pso_->SetView(&desc_set, "ngl_cb_sceneview", desc_.ref_scene_cbv.Get());
				
				gfx_commandlist->SetPipelineState(pso_.Get());
				gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

				pso_->DispatchHelper(gfx_commandlist, res_linear_depth.tex_->GetWidth(), res_linear_depth.tex_->GetHeight(), 1);
			}

		};


		// Lightingパス.
		struct TaskLightPass : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_gb0_{};
			rtg::RtgResourceHandle h_gb1_{};
			rtg::RtgResourceHandle h_gb2_{};
			rtg::RtgResourceHandle h_gb3_{};
			rtg::RtgResourceHandle h_velocity_{};
			rtg::RtgResourceHandle h_linear_depth_{};
			rtg::RtgResourceHandle h_prev_light_{};
			rtg::RtgResourceHandle h_light_{};
			
			rtg::RtgResourceHandle h_shadowmap_{};
			
			
			rhi::RhiRef<rhi::GraphicsPipelineStateDep> pso_;

			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};
				rhi::RefCbvDep ref_shadow_cbv{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				rtg::RtgResourceHandle h_gb0, rtg::RtgResourceHandle h_gb1, rtg::RtgResourceHandle h_gb2, rtg::RtgResourceHandle h_gb3, rtg::RtgResourceHandle h_velocity,
				rtg::RtgResourceHandle h_linear_depth, rtg::RtgResourceHandle h_prev_light,
				rtg::RtgResourceHandle h_shadowmap,
				rtg::RtgResourceHandle h_async_compute_result,
				const SetupDesc& desc)
			{
				// Rtgリソースセットアップ.
				rtg::RtgResourceDesc2D light_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
				{
					desc_ = desc;
					
					// リソースアクセス定義.
					h_gb0_ = builder.RecordResourceAccess(*this, h_gb0, rtg::access_type::SHADER_READ);
					h_gb1_ = builder.RecordResourceAccess(*this, h_gb1, rtg::access_type::SHADER_READ);
					h_gb2_ = builder.RecordResourceAccess(*this, h_gb2, rtg::access_type::SHADER_READ);
					h_gb3_ = builder.RecordResourceAccess(*this, h_gb3, rtg::access_type::SHADER_READ);
					h_velocity_ = builder.RecordResourceAccess(*this, h_velocity, rtg::access_type::SHADER_READ);
					h_linear_depth_ = builder.RecordResourceAccess(*this, h_linear_depth, rtg::access_type::SHADER_READ);
					h_shadowmap_ = builder.RecordResourceAccess(*this, h_shadowmap, rtg::access_type::SHADER_READ);

					h_prev_light_ = {};
					if(!h_prev_light.IsInvalid())
					{
						h_prev_light_ = builder.RecordResourceAccess(*this, h_prev_light, rtg::access_type::SHADER_READ);
					}
					if(!h_async_compute_result.IsInvalid())
					{
						// Asyncの結果を読み取りだけレコードしてFenceさせる.
						builder.RecordResourceAccess(*this, h_async_compute_result, rtg::access_type::SHADER_READ);
					}
				
					h_light_ = builder.RecordResourceAccess(*this, builder.CreateResource(light_desc), rtg::access_type::RENDER_TARTGET);// このTaskで新規生成したRenderTargetを出力先とする.
				}
				
				{
					// 初期化. シェーダバイナリの要求とPSO生成.
					
					auto& ResourceMan = ngl::res::ResourceManager::Instance();

					ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
					{
						loaddesc_vs.entry_point_name = "main_vs";
						loaddesc_vs.stage = ngl::rhi::EShaderStage::Vertex;
						loaddesc_vs.shader_model_version = k_shader_model;
					}
					auto res_shader_vs = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/screen/fullscr_procedural_vs.hlsl", &loaddesc_vs);

					ngl::gfx::ResShader::LoadDesc loaddesc_ps = {};
					{
						loaddesc_ps.entry_point_name = "main_ps";
						loaddesc_ps.stage = ngl::rhi::EShaderStage::Pixel;
						loaddesc_ps.shader_model_version = k_shader_model;
					}
					auto res_shader_ps = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/df_light_pass_ps.hlsl", &loaddesc_ps);

					const auto light_desc = builder.GetResourceHandleDesc(h_light_);

					ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
					{
						desc.vs = &res_shader_vs->data_;
						desc.ps = &res_shader_ps->data_;
						{
							desc.num_render_targets = 1;
							desc.render_target_formats[0] = light_desc.desc.format;
						}
						{
							//desc.depth_stencil_state.depth_enable;
						}
					}
					pso_ = new rhi::GraphicsPipelineStateDep();
					if (!pso_->Initialize(p_device, desc))
					{
						assert(false);
					}
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "Lighting");
				
				auto& global_res = gfx::GlobalRenderResource::Instance();
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_gb0 = builder.GetAllocatedResource(this, h_gb0_);
				auto res_gb1 = builder.GetAllocatedResource(this, h_gb1_);
				auto res_gb2 = builder.GetAllocatedResource(this, h_gb2_);
				auto res_gb3 = builder.GetAllocatedResource(this, h_gb3_);
				auto res_velocity = builder.GetAllocatedResource(this, h_velocity_);

				auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
				auto res_prev_light = builder.GetAllocatedResource(this, h_prev_light_);// 前回フレームリソースのテスト.
				auto res_light = builder.GetAllocatedResource(this, h_light_);
				auto res_shadowmap = builder.GetAllocatedResource(this, h_shadowmap_);
				
				assert(res_gb0.tex_.IsValid() && res_gb0.srv_.IsValid());
				assert(res_gb1.tex_.IsValid() && res_gb1.srv_.IsValid());
				assert(res_gb2.tex_.IsValid() && res_gb2.srv_.IsValid());
				assert(res_gb3.tex_.IsValid() && res_gb3.srv_.IsValid());
				assert(res_velocity.tex_.IsValid() && res_velocity.srv_.IsValid());
				assert(res_linear_depth.tex_.IsValid() && res_linear_depth.srv_.IsValid());
				assert(res_light.tex_.IsValid() && res_light.rtv_.IsValid());
				assert(res_shadowmap.tex_.IsValid() && res_shadowmap.srv_.IsValid());

				rhi::RefSrvDep ref_prev_lit = (res_prev_light.srv_.IsValid())? res_prev_light.srv_ : global_res.default_resource_.tex_black->ref_view_;
				
				// Viewport.
				gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_light.tex_->GetWidth(), res_light.tex_->GetHeight());

				// Rtv, Dsv セット.
				{
					const auto* p_rtv = res_light.rtv_.Get();
					gfx_commandlist->SetRenderTargets(&p_rtv, 1, nullptr);
				}

				gfx_commandlist->SetPipelineState(pso_.Get());
				ngl::rhi::DescriptorSetDep desc_set = {};

				pso_->SetView(&desc_set, "ngl_cb_sceneview", desc_.ref_scene_cbv.Get());
				pso_->SetView(&desc_set, "ngl_cb_shadowview", desc_.ref_shadow_cbv.Get());
				
				pso_->SetView(&desc_set, "tex_lineardepth", res_linear_depth.srv_.Get());
				pso_->SetView(&desc_set, "tex_gbuffer0", res_gb0.srv_.Get());
				pso_->SetView(&desc_set, "tex_gbuffer1", res_gb1.srv_.Get());
				pso_->SetView(&desc_set, "tex_gbuffer2", res_gb2.srv_.Get());
				pso_->SetView(&desc_set, "tex_gbuffer3", res_gb3.srv_.Get());
				
				pso_->SetView(&desc_set, "tex_prev_light", ref_prev_lit.Get());

				pso_->SetView(&desc_set, "tex_shadowmap", res_shadowmap.srv_.Get());
				
				pso_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
				pso_->SetView(&desc_set, "samp_shadow", gfx::GlobalRenderResource::Instance().default_resource_.sampler_shadow_linear.Get());
				
				gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

				gfx_commandlist->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
				gfx_commandlist->DrawInstanced(3, 1, 0, 0);
			}
		};


		// 最終パス.
		struct TaskFinalPass : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_depth_{};
			rtg::RtgResourceHandle h_linear_depth_{};
			rtg::RtgResourceHandle h_light_{};
			rtg::RtgResourceHandle h_swapchain_{};

			rtg::RtgResourceHandle h_other_rtg_out_{};// 先行する別rtgがPropagateしたハンドルをそのフレームの後段のrtgで使用するテスト.
			rtg::RtgResourceHandle h_rt_result_{};
			
			rtg::RtgResourceHandle h_gbuffer0_{};// Debug View用
			rtg::RtgResourceHandle h_gbuffer1_{};// Debug View用
			rtg::RtgResourceHandle h_gbuffer2_{};// Debug View用
			rtg::RtgResourceHandle h_gbuffer3_{};// Debug View用
			rtg::RtgResourceHandle h_dshadow_{};// Debug View用
			
			rtg::RtgResourceHandle h_tmp_{}; // 一時リソーステスト. マクロにも登録しない.
			
			rhi::RhiRef<rhi::GraphicsPipelineStateDep> pso_;

			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};

				bool debugview_halfdot_gray = false;
				bool debugview_subview_result = false;
				bool debugview_raytrace_result = false;
				
				bool debugview_gbuffer = false;
				bool debugview_dshadow = false;
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				rtg::RtgResourceHandle h_swapchain, rtg::RtgResourceHandle h_depth, rtg::RtgResourceHandle h_linear_depth, rtg::RtgResourceHandle h_light,
				rtg::RtgResourceHandle h_other_rtg_out, rtg::RtgResourceHandle h_rt_result,
				rtg::RtgResourceHandle h_gbuffer0, rtg::RtgResourceHandle h_gbuffer1, rtg::RtgResourceHandle h_gbuffer2, rtg::RtgResourceHandle h_gbuffer3,
				rtg::RtgResourceHandle h_dshadow,
				const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::SHADER_READ);
					h_linear_depth_ = builder.RecordResourceAccess(*this, h_linear_depth, rtg::access_type::SHADER_READ);
					h_light_ = builder.RecordResourceAccess(*this, h_light, rtg::access_type::SHADER_READ);

					h_swapchain_ = builder.RecordResourceAccess(*this, h_swapchain, rtg::access_type::RENDER_TARTGET);

					if(!h_rt_result.IsInvalid())
					{
						h_rt_result_ = builder.RecordResourceAccess(*this, h_rt_result, rtg::access_type::SHADER_READ);
					}
					if(!h_other_rtg_out.IsInvalid())
					{
						h_other_rtg_out_ = builder.RecordResourceAccess(*this, h_other_rtg_out, rtg::access_type::SHADER_READ);
					}

					if(!h_gbuffer0.IsInvalid())
						h_gbuffer0_ = builder.RecordResourceAccess(*this, h_gbuffer0, rtg::access_type::SHADER_READ);
					if(!h_gbuffer1.IsInvalid())
						h_gbuffer1_ = builder.RecordResourceAccess(*this, h_gbuffer1, rtg::access_type::SHADER_READ);
					if(!h_gbuffer2.IsInvalid())
						h_gbuffer2_ = builder.RecordResourceAccess(*this, h_gbuffer2, rtg::access_type::SHADER_READ);
					if(!h_gbuffer3.IsInvalid())
						h_gbuffer3_ = builder.RecordResourceAccess(*this, h_gbuffer3, rtg::access_type::SHADER_READ);
					if(!h_dshadow.IsInvalid())
						h_dshadow_ = builder.RecordResourceAccess(*this, h_dshadow, rtg::access_type::SHADER_READ);
					
					// リソースアクセス期間による再利用のテスト用. 作業用の一時リソース.
					rtg::RtgResourceDesc2D temp_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R11G11B10_FLOAT);
					auto temp_res0 = builder.RecordResourceAccess(*this, builder.CreateResource(temp_desc), rtg::access_type::RENDER_TARTGET);
					h_tmp_ = temp_res0;
				}
				
				// pso生成のためにRenderTarget(実際はSwapchain)のDescをBuilderから取得. DescはCompile前に取得ができるものとする.
				// (実リソース再利用割当のために実際のリソースのWidthやHeightは取得できないが...).
				const auto render_target_desc = builder.GetResourceHandleDesc(h_swapchain);

				{
					// 初期化. シェーダバイナリの要求とPSO生成.

					auto& ResourceMan = ngl::res::ResourceManager::Instance();

					ngl::gfx::ResShader::LoadDesc loaddesc_vs = {};
					{
						loaddesc_vs.entry_point_name = "main_vs";
						loaddesc_vs.stage = ngl::rhi::EShaderStage::Vertex;
						loaddesc_vs.shader_model_version = k_shader_model;
					}
					auto res_shader_vs = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/screen/fullscr_procedural_vs.hlsl", &loaddesc_vs);

					ngl::gfx::ResShader::LoadDesc loaddesc_ps = {};
					{
						loaddesc_ps.entry_point_name = "main_ps";
						loaddesc_ps.stage = ngl::rhi::EShaderStage::Pixel;
						loaddesc_ps.shader_model_version = k_shader_model;
					}
					auto res_shader_ps = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/final_screen_pass_ps.hlsl", &loaddesc_ps);


					ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
					desc.vs = &res_shader_vs->data_;
					desc.ps = &res_shader_ps->data_;

					desc.num_render_targets = 1;
					desc.render_target_formats[0] = render_target_desc.desc.format;

					desc.blend_state.target_blend_states[0].blend_enable = false;
					desc.blend_state.target_blend_states[0].write_mask = ~ngl::u8(0);

					pso_ = new rhi::GraphicsPipelineStateDep();
					if (!pso_->Initialize(p_device, desc))
					{
						assert(false);
					}
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "Final");
				
				auto& global_res = gfx::GlobalRenderResource::Instance();
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_depth = builder.GetAllocatedResource(this, h_depth_);
				auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
				auto res_light = builder.GetAllocatedResource(this, h_light_);
				auto res_swapchain = builder.GetAllocatedResource(this, h_swapchain_);
				auto res_tmp = builder.GetAllocatedResource(this, h_tmp_);
				auto res_other_rtg_out = builder.GetAllocatedResource(this, h_other_rtg_out_);
				auto res_rt_result = builder.GetAllocatedResource(this, h_rt_result_);
				
				auto res_gbuffer0 = builder.GetAllocatedResource(this, h_gbuffer0_);
				auto res_gbuffer1 = builder.GetAllocatedResource(this, h_gbuffer1_);
				auto res_gbuffer2 = builder.GetAllocatedResource(this, h_gbuffer2_);
				auto res_gbuffer3 = builder.GetAllocatedResource(this, h_gbuffer3_);
				auto res_dshadow = builder.GetAllocatedResource(this, h_dshadow_);

				assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
				assert(res_linear_depth.tex_.IsValid() && res_linear_depth.srv_.IsValid());
				assert(res_light.tex_.IsValid() && res_light.srv_.IsValid());
				assert(res_swapchain.swapchain_.IsValid() && res_swapchain.rtv_.IsValid());
				assert(res_tmp.tex_.IsValid() && res_tmp.rtv_.IsValid());

				rhi::RefSrvDep ref_other_rtg_out{};
				if(res_other_rtg_out.srv_.IsValid())
				{
					ref_other_rtg_out = res_other_rtg_out.srv_;
				}
				else
				{
					ref_other_rtg_out = global_res.default_resource_.tex_red->ref_view_;
				}
				
				rhi::RefSrvDep ref_rt_result{};
				if(res_rt_result.srv_.IsValid())
				{
					ref_rt_result = res_rt_result.srv_;
				}
				else
				{
					ref_rt_result = global_res.default_resource_.tex_green->ref_view_;
				}

				rhi::RefSrvDep ref_gbuffer0 = (res_gbuffer0.srv_.IsValid())? res_gbuffer0.srv_ : global_res.default_resource_.tex_black->ref_view_;
				rhi::RefSrvDep ref_gbuffer1 = (res_gbuffer1.srv_.IsValid())? res_gbuffer1.srv_ : global_res.default_resource_.tex_black->ref_view_;
				rhi::RefSrvDep ref_gbuffer2 = (res_gbuffer2.srv_.IsValid())? res_gbuffer2.srv_ : global_res.default_resource_.tex_black->ref_view_;
				rhi::RefSrvDep ref_gbuffer3 = (res_gbuffer3.srv_.IsValid())? res_gbuffer3.srv_ : global_res.default_resource_.tex_black->ref_view_;
				rhi::RefSrvDep ref_dshadow = (res_dshadow.srv_.IsValid())? res_dshadow.srv_ : global_res.default_resource_.tex_black->ref_view_;

				
				struct CbFinalScreenPass
				{
					int enable_halfdot_gray;
					int enable_subview_result;
					int enable_raytrace_result;
					int enable_gbuffer;
					int enable_dshadow;
				};
				rhi::RefBufferDep ref_cb = new rhi::BufferDep();
				rhi::RefCbvDep ref_cbv = new rhi::ConstantBufferViewDep();
				{
					{
						rhi::BufferDep::Desc cb_desc{};
						cb_desc.SetupAsConstantBuffer(sizeof(CbFinalScreenPass));
						ref_cb->Initialize(gfx_commandlist->GetDevice(), cb_desc);
					}
					{
						rhi::ConstantBufferViewDep::Desc cbv_desc{};
						ref_cbv->Initialize(ref_cb.Get(), cbv_desc);
					}
					if(auto* p_mapped = ref_cb->MapAs<CbFinalScreenPass>())
					{
						p_mapped->enable_halfdot_gray = desc_.debugview_halfdot_gray;
						p_mapped->enable_subview_result = desc_.debugview_subview_result;
						p_mapped->enable_raytrace_result = desc_.debugview_raytrace_result;

						p_mapped->enable_gbuffer = desc_.debugview_gbuffer;
						p_mapped->enable_dshadow = desc_.debugview_dshadow;

						ref_cb->Unmap();
					}
				}
				
				gfx::helper::SetFullscreenViewportAndScissor(gfx_commandlist, res_swapchain.swapchain_->GetWidth(), res_swapchain.swapchain_->GetHeight());

				// Rtv, Dsv セット.
				{
					const auto* p_rtv = res_swapchain.rtv_.Get();
					gfx_commandlist->SetRenderTargets(&p_rtv, 1, nullptr);
				}

				gfx_commandlist->SetPipelineState(pso_.Get());
				ngl::rhi::DescriptorSetDep desc_set = {};
				pso_->SetView(&desc_set, "cb_final_screen_pass", ref_cbv.Get());
				pso_->SetView(&desc_set, "tex_light", res_light.srv_.Get());
				pso_->SetView(&desc_set, "tex_rt", ref_rt_result.Get());
				pso_->SetView(&desc_set, "tex_res_data", ref_other_rtg_out.Get());

				pso_->SetView(&desc_set, "tex_gbuffer0", ref_gbuffer0.Get());
				pso_->SetView(&desc_set, "tex_gbuffer1", ref_gbuffer1.Get());
				pso_->SetView(&desc_set, "tex_gbuffer2", ref_gbuffer2.Get());
				pso_->SetView(&desc_set, "tex_gbuffer3", ref_gbuffer3.Get());
				pso_->SetView(&desc_set, "tex_dshadow", ref_dshadow.Get());
				
				pso_->SetView(&desc_set, "samp", gfx::GlobalRenderResource::Instance().default_resource_.sampler_linear_wrap.Get());
				gfx_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);

				gfx_commandlist->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
				gfx_commandlist->DrawInstanced(3, 1, 0, 0);
			}
		};


		// AsyncComputeテスト用タスク (IComputeTaskNode派生).
		struct TaskCopmuteTest : public  rtg::IComputeTaskNode
		{
			rtg::RtgResourceHandle h_work_tex_{};

			ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ = {};

			struct SetupDesc
			{
				int w{};
				int h{};
				
				rhi::RefCbvDep ref_scene_cbv{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info,
				rtg::RtgResourceHandle h_input_test, const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					// リソース定義.
					rtg::RtgResourceDesc2D work_tex_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);

					// リソースアクセス定義.
					h_work_tex_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_tex_desc), rtg::access_type::UAV);

					// 入力リソーステスト.
					if(!h_input_test.IsInvalid())
						builder.RecordResourceAccess(*this, h_input_test, rtg::access_type::SHADER_READ);
				}

				{
					pso_ = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
					{
						ngl::rhi::ComputePipelineStateDep::Desc cpso_desc = {};
						{
							ngl::gfx::ResShader::LoadDesc cs_load_desc = {};
							cs_load_desc.stage = ngl::rhi::EShaderStage::Compute;
							cs_load_desc.shader_model_version = k_shader_model;
							cs_load_desc.entry_point_name = "main_cs";
							auto cs_load_handle = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(
								p_device, "./src/ngl/data/shader/test/async_task_test_cs.hlsl", &cs_load_desc
							);
							cpso_desc.cs = &cs_load_handle->data_;
						}
						pso_->Initialize(p_device, cpso_desc);
					}
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::ComputeCommandListDep* p_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(p_commandlist, "ComputeTest");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_work_tex = builder.GetAllocatedResource(this, h_work_tex_);

				assert(res_work_tex.tex_.IsValid() && res_work_tex.uav_.IsValid());

				p_commandlist->SetPipelineState(pso_.Get());
				
				ngl::rhi::DescriptorSetDep desc_set = {};
				pso_->SetView(&desc_set, "rwtex_out", res_work_tex.uav_.Get());
				p_commandlist->SetDescriptorSet(pso_.Get(), &desc_set);
				
				pso_->DispatchHelper(p_commandlist, res_work_tex.tex_->GetWidth(), res_work_tex.tex_->GetHeight(), 1);
			}
		};

		
		// Raytracing Pass.
		struct TaskRtDispatch : public rtg::IGraphicsTaskNode
		{
			rtg::RtgResourceHandle h_rt_result_{};
			
			ngl::res::ResourceHandle <ngl::gfx::ResShader> res_shader_lib_;
			gfx::RtPassCore	rt_pass_core_ = {};
			
			struct SetupDesc
			{
				int w{};
				int h{};
				
				class gfx::RtSceneManager* p_rt_scene{};
			};
			SetupDesc desc_{};
			
			// リソースとアクセスを定義するプリプロセス.
			void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const RenderPassViewInfo& view_info, const SetupDesc& desc)
			{
				desc_ = desc;
				
				// Rtgリソースセットアップ.
				{
					rtg::RtgResourceDesc2D res_desc = rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R16G16B16A16_FLOAT);
					// リソースアクセス定義.
					h_rt_result_ = builder.RecordResourceAccess(*this, builder.CreateResource(res_desc), rtg::access_type::UAV);
				}
				
				// Raytrace Pipelineセットアップ.
				{
					{
						auto& ResourceMan = ngl::res::ResourceManager::Instance();

						ngl::gfx::ResShader::LoadDesc loaddesc = {};
						loaddesc.stage = ngl::rhi::EShaderStage::ShaderLibrary;
						loaddesc.shader_model_version = "6_3";
						res_shader_lib_ = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./src/ngl/data/shader/dxr_sample_lib.hlsl", &loaddesc);
					}

					// StateObject生成.
					std::vector<ngl::gfx::RtShaderRegisterInfo> shader_reg_info_array = {};
					{
						// Shader登録エントリ新規.
						auto shader_index = shader_reg_info_array.size();
						shader_reg_info_array.push_back({});

						// ShaderLibバイナリ.
						shader_reg_info_array[shader_index].p_shader_library = &res_shader_lib_->data_;

						// シェーダから公開するRayGen名.
						shader_reg_info_array[shader_index].ray_generation_shader_array.push_back("rayGen");

						// シェーダから公開するMissShader名.
						shader_reg_info_array[shader_index].miss_shader_array.push_back("miss");
						shader_reg_info_array[shader_index].miss_shader_array.push_back("miss2");

						// HitGroup関連情報.
						{
							auto hg_index = shader_reg_info_array[shader_index].hitgroup_array.size();
							shader_reg_info_array[shader_index].hitgroup_array.push_back({});

							shader_reg_info_array[shader_index].hitgroup_array[hg_index].hitgorup_name = "hitGroup";
							// このHitGroupはClosestHitのみ.
							shader_reg_info_array[shader_index].hitgroup_array[hg_index].closest_hit_name = "closestHit";
						}
						{
							auto hg_index = shader_reg_info_array[shader_index].hitgroup_array.size();
							shader_reg_info_array[shader_index].hitgroup_array.push_back({});

							shader_reg_info_array[shader_index].hitgroup_array[hg_index].hitgorup_name = "hitGroup2";
							// このHitGroupはClosestHitのみ.
							shader_reg_info_array[shader_index].hitgroup_array[hg_index].closest_hit_name = "closestHit2";
						}
					}

					const uint32_t payload_byte_size = sizeof(float) * 4;// Payloadのサイズ.
					const uint32_t attribute_byte_size = sizeof(float) * 2;// BuiltInTriangleIntersectionAttributes の固定サイズ.
					constexpr uint32_t max_trace_recursion = 1;
					if (!rt_pass_core_.InitializeBase(p_device, shader_reg_info_array, payload_byte_size, attribute_byte_size, max_trace_recursion ))
					{
						assert(false);
					}
				}
			}

			// レンダリング処理.
			void Run(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
			{
				NGL_SCOPED_EVENT_MARKER(gfx_commandlist, "RtPass");
				
				// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
				auto res_rt_result = builder.GetAllocatedResource(this, h_rt_result_);
				assert(res_rt_result.tex_.IsValid() && res_rt_result.uav_.IsValid());

				// 正常に初期化されていなければ終了.
				if(!desc_.p_rt_scene->IsValid())
					return;
				
				// Rt ShaderTable更新.
				rt_pass_core_.UpdateScene(desc_.p_rt_scene, "rayGen");

				// Ray Dispatch.
				{
					gfx::RtPassCore::DispatchRayParam param = {};
					param.count_x = res_rt_result.tex_->GetWidth();
					param.count_y = res_rt_result.tex_->GetHeight();
					// global resourceのセット.
					{
						param.cbv_slot[0] = desc_.p_rt_scene->GetSceneViewCbv();// View.
					}
					{
						param.srv_slot;
					}
					{
						param.uav_slot[0] = res_rt_result.uav_.Get();//出力UAV.
					}
					{
						param.sampler_slot;
					}

					// dispatch.
					rt_pass_core_.DispatchRay(gfx_commandlist, param);
				}
			}
		};

		
	}
}


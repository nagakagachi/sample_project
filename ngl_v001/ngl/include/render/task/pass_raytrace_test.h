/*
    Raytracing Pass.
*/

#pragma once

#include "pass_common.h"

#include "gfx/rtg/graph_builder.h"
#include "gfx/command_helper.h"

#include "gfx/raytrace/raytrace_scene.h"

namespace ngl::render::task
{
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
		} desc_{};
		bool is_render_skip_debug{};
		
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
					res_shader_lib_ = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, NGL_RENDER_SHADER_PATH("dxr_sample_lib.hlsl"), &loaddesc);
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
			
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					if(is_render_skip_debug)
					{
						return;
					}
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RtPass");
						
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_rt_result = builder.GetAllocatedResource(this, h_rt_result_);
					assert(res_rt_result.tex_.IsValid() && res_rt_result.uav_.IsValid());

					// 正常に初期化されていなければ終了.
					if(!desc_.p_rt_scene->IsValid())
						return;
						
					// Rt ShaderTable更新.
					rt_pass_core_.UpdateScene(desc_.p_rt_scene, "rayGen");


					struct RaytraceInfo
					{
						// レイタイプの種類数, (== hitgroup数). ShaderTable構築時に登録されたHitgroup数.
						//	TraceRay()での multiplier_for_subgeometry_index に使用するために必要とされる.
						//		ex) Primary, Shadow の2種であれば 2.
						int num_ray_type;
					};
					auto raytrace_cbh = gfx_commandlist->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(RaytraceInfo));
					if(auto* mapped = raytrace_cbh->buffer_.MapAs<RaytraceInfo>())
					{
						mapped->num_ray_type = desc_.p_rt_scene->NumHitGroupCountMax();
						
						raytrace_cbh->buffer_.Unmap();
					}
					
					// Ray Dispatch.
					{
						gfx::RtPassCore::DispatchRayParam param = {};
						param.count_x = res_rt_result.tex_->GetWidth();
						param.count_y = res_rt_result.tex_->GetHeight();
						// global resourceのセット.
						{
							param.cbv_slot[0] = desc_.p_rt_scene->GetSceneViewCbv();// View.
							param.cbv_slot[1] = &raytrace_cbh->cbv_;
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
				});
		}
	};

}
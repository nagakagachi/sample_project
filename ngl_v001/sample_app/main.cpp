
#include <iostream>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>

#include <thread>


#include "boot/boot_application.h"
#include "platform/window.h"
#include "util/time/timer.h"
#include "file/file.h"

#include "util/bit_operation.h"

#include "math/math.h"

#include "thread/job_thread.h"
#include "thread/test_lockfree_stack.h"

// resource
#include "resource/resource_manager.h"

// rhi
#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/descriptor.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

#include "rhi/constant_buffer_pool.h"

// gfx
#include "gfx/render/global_render_resource.h"
#include "gfx/raytrace_scene.h"
#include "gfx/mesh_component.h"


// Render Path
#include "render/test_render_path.h"

// マテリアルシェーダ関連.
#include "gfx/material/material_shader_generator.h"
#include "gfx/material/material_shader_manager.h"


// imguiのシステム処理Wrapper.
#include "imgui/imgui_interface.h"



// ImGui.
static bool dbgw_test_window_enable = true;
static bool dbgw_enable_render_thread = true;
static bool dbgw_enable_pass_render_parallel = true;
static float dbgw_perf_main_thread_sleep_millisec = 0.0f;
static bool dbgw_enable_feedback_blur_test = true;
static bool dbgw_enable_sub_view_path = false;
static bool dbgw_enable_raytrace_pass = false;
static bool dbgw_view_half_dot_gray = false;
static bool dbgw_view_gbuffer = true;
static bool dbgw_view_dshadow = true;
static float dbgw_dlit_angle_v = 0.4f;
static float dbgw_dlit_angle_h = 4.1f;

static float dbgw_stat_primary_rtg_construct = {};
static float dbgw_stat_primary_rtg_compile = {};
static float dbgw_stat_primary_rtg_execute = {};


class PlayerController
{
private:
	bool prev_mouse_r_ = false;
public:
	ngl::math::Mat33 camera_pose_{};
	ngl::math::Vec3 camera_pos_{};
	
	void UpdateFrame(ngl::platform::CoreWindow& window, float delta_sec, const ngl::math::Mat33& prev_camera_pose, const ngl::math::Vec3& prev_camera_pos);
};


// アプリ本体.
class AppGame : public ngl::boot::ApplicationBase
{
public:
	AppGame();
	~AppGame();

	bool Initialize() override;
	bool Execute() override;

	// フレーム開始タイミング.
	void BeginFrame();
	// RenderThread同期.
	void SyncRender();
	// RenderThreadメイン処理 (RenderThread側).
	void LaunchRender();
	
private:
	struct RtgGenerateCommandListSet
	{
		std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> graphics{};
		std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> compute{};
	};
	// RenderThreadメイン処理 (RenderThread側).
	void RenderApp(std::vector<RtgGenerateCommandListSet>& out_rtg_command_list_set);
private:
	struct RenderParam
	{
		ngl::math::Vec3		camera_pos = {0.0f, 2.0f, -1.0f};
		ngl::math::Mat33	camera_pose = ngl::math::Mat33::Identity();
		float				camera_fov_y = ngl::math::Deg2Rad(60.0f);// not half fov.
		
		ngl::gfx::SceneRepresentation	frame_scene{};
		ngl::math::Vec3					dlight_dir{};

		// any.
	};
	using RenderParamPtr = std::shared_ptr<RenderParam>;
	RenderParamPtr pushed_render_param_{};// RenderThreadでへ送付するための一時データ.
	RenderParamPtr render_param_{};// RenderThreadで読み取り可能なRenderParam.
	void PushRenderParam(RenderParamPtr param);
	void SyncRenderParam();
	
private:
	double						app_sec_ = 0.0f;
	double						frame_sec_ = 0.0f;

	ngl::platform::CoreWindow	window_;

	// RenderThread.
	ngl::thread::SingleJobThread				render_thread_;
	
	ngl::rhi::DeviceDep							device_;
	ngl::rhi::GraphicsCommandQueueDep			graphics_queue_;
	ngl::rhi::ComputeCommandQueueDep			compute_queue_;
	
	// CommandQueue実行完了待機用Fence
	ngl::rhi::FenceDep							gpu_wait_fence_;
	// CommandQueue実行完了待機用オブジェクト
	ngl::rhi::WaitOnFenceSignalDep				gpu_wait_signal_;
	
	// GPU待機用バッファの最大数. 内部バッファ確保のためのサイズ.
	static constexpr int							k_gpu_work_queue_count_max = 4;
	// SubmitしたGPUタスクのバッファリング待機用情報.
	std::array<ngl::u64, k_gpu_work_queue_count_max>	inflight_gpu_work_id_{};
	std::array<bool, k_gpu_work_queue_count_max>		inflight_gpu_work_id_enable_{};
	// GPU側の待機キューの数. value =< バックバッファ数.
	//	理想的にはバックバッファ数と同じ値だが, 現状RHIのガベージコレクションなどがGPU側1F遅れと想定しているため 1 を指定している.
	//	ガベコレにGPUにSubmitしたフレームIDを識別して破棄する仕組みを入れれば最大限増加してパフォーマンス向上できると考えられる.
	static constexpr ngl::u32 inflight_gpu_work_flip_count_ = 1;
	ngl::u32 inflight_gpu_work_flip_ = 0;



	ngl::rhi::GraphicsCommandListDep*			p_gfx_frame_begin_command_list_{};

	// SwapChain
	ngl::rhi::RhiRef<ngl::rhi::SwapChainDep>	swapchain_;
	std::vector<ngl::rhi::RefRtvDep>			swapchain_rtvs_;
	std::vector<ngl::rhi::EResourceState>		swapchain_resource_state_;

	// RenderTaskGraphのCompileやそれらが利用するリソースプール管理.
	ngl::rtg::RenderTaskGraphManager			rtg_manager_{};

	
	ngl::rhi::RefTextureDep						tex_rw_;
	ngl::rhi::RefSrvDep							tex_rw_srv_;
	ngl::rhi::RefUavDep							tex_rw_uav_;
	
	// Loaded Texture.
	ngl::res::ResourceHandle<ngl::gfx::ResTexture> res_texture_{};

	// Meshオブジェクト管理.
	std::vector<std::shared_ptr<ngl::gfx::StaticMeshComponent>>	mesh_comp_array_;
	std::vector<ngl::gfx::StaticMeshComponent*>	test_move_mesh_comp_array_;
	
	// RaytraceScene.
	ngl::gfx::RtSceneManager					rt_scene_;

	
	ngl::math::Vec3		camera_pos_ = {0.0f, 2.0f, -1.0f};
	ngl::math::Mat33	camera_pose_ = ngl::math::Mat33::Identity();
	float				camera_fov_y = ngl::math::Deg2Rad(60.0f);// not half fov.
	PlayerController	player_controller{};

};


// -----------------------------------------------
// テストコードの呼び出しはここに書く.
static void TestEntry()
{
	// テストコード呼び出し.
	ngl::thread::TestLockFreeStackIntrusive();
	ngl::thread::TestFixedSizeLockFreeStack();
	ngl::thread::TestStaticSizeLockFreeStack();
}


AppGame::AppGame()
{
	
	TestEntry();

}
AppGame::~AppGame()
{
	// RenderThread待機.
	render_thread_.Wait();
	
	// GPUへ投入したタスクの完了待ち.
	for(ngl::u32 gpu_work_offset = 0; gpu_work_offset < inflight_gpu_work_flip_count_; ++gpu_work_offset)
	{
		const auto gpu_work_index = (inflight_gpu_work_flip_ + gpu_work_offset) % inflight_gpu_work_flip_count_;
		if (inflight_gpu_work_id_enable_[gpu_work_index])
		{
			gpu_wait_signal_.Wait(&gpu_wait_fence_, inflight_gpu_work_id_[gpu_work_index]);
			inflight_gpu_work_id_enable_[gpu_work_index] = false;
		}
	}

	rt_scene_ = {};

	// リソース参照クリア.
	mesh_comp_array_.clear();

	// Material Shader Manager.
	ngl::gfx::MaterialShaderManager::Instance().Finalize();
	// 共有リソース管理.
	ngl::gfx::GlobalRenderResource::Instance().Finalize();
	
	// リソースマネージャから全て破棄.
	ngl::res::ResourceManager::Instance().ReleaseCacheAll();

	// imgui.
	ngl::imgui::ImguiInterface::Instance().Finalize();
	
	swapchain_.Reset();
	graphics_queue_.Finalize();
	compute_queue_.Finalize();
	device_.Finalize();
}

bool AppGame::Initialize()
{
	constexpr auto scree_h = 1080;
	constexpr auto scree_w = scree_h * 16/9;

	// ウィンドウ作成
	if (!window_.Initialize(_T("ToyRenderer"), scree_w, scree_h))
	{
		return false;
	}
	
	// Graphics Device.
	{
		ngl::rhi::DeviceDep::Desc device_desc{};
		#if _DEBUG
			device_desc.enable_debug_layer = true;	// デバッグレイヤ有効化.
		#endif
		device_desc.frame_descriptor_size = 500000;
		device_desc.persistent_descriptor_size = 500000;
		if (!device_.Initialize(&window_, device_desc))
		{
			std::cout << "[ERROR] Initialize Device" << std::endl;
			return false;
		}
		
		// Raytracing Support Check.
		if (!device_.IsSupportDxr())
		{
			MessageBoxA(window_.Dep().GetWindowHandle(), "Raytracing is not supported on this device.", "Info", MB_OK);
		}

		// GpuWorkId管理バッファサイズチェック.
		assert(k_gpu_work_queue_count_max > device_.GetDesc().swapchain_buffer_count);
		assert(device_.GetDesc().swapchain_buffer_count >= inflight_gpu_work_flip_count_);

	}
	// graphics queue.
	if (!graphics_queue_.Initialize(&device_))
	{
		std::cout << "[ERROR] Initialize Graphics Command Queue" << std::endl;
		return false;
	}
	// compute queue.
	if(!compute_queue_.Initialize(&device_))
	{
		std::cout << "[ERROR] Initialize Compute Command Queue" << std::endl;
		return false;
	}
	// swapchain.
	{
		ngl::rhi::SwapChainDep::Desc swap_chain_desc;
		swap_chain_desc.format = ngl::rhi::EResourceFormat::Format_R10G10B10A2_UNORM;
		swapchain_ = new ngl::rhi::SwapChainDep();
		if (!swapchain_->Initialize(&device_, &graphics_queue_, swap_chain_desc))
		{
			std::cout << "[ERROR] Initialize SwapChain" << std::endl;
			return false;
		}

		swapchain_rtvs_.resize(swapchain_->NumResource());
		swapchain_resource_state_.resize(swapchain_->NumResource());
		for (auto i = 0u; i < swapchain_->NumResource(); ++i)
		{
			swapchain_rtvs_[i] = new ngl::rhi::RenderTargetViewDep();
			swapchain_rtvs_[i]->Initialize(&device_, swapchain_.Get(), i);
			swapchain_resource_state_[i] = ngl::rhi::EResourceState::Common;// Swapchain初期ステートは指定していないためCOMMON状態.
		}
	}
	
	if (!gpu_wait_fence_.Initialize(&device_))
	{
		std::cout << "[ERROR] Initialize Fence" << std::endl;
		return false;
	}
	
	if(false)
	{
		// ConstantBufferPool(テスト)
		ngl::rhi::ConstantBufferPool* cb_pool = device_.GetConstantBufferPool();
		
		auto cbh_2 = cb_pool->Alloc(2);
		cbh_2 = {};
		auto cbh0 = cb_pool->Alloc(15);
		auto cbh1 = cb_pool->Alloc(16);
		auto cbh2 = cb_pool->Alloc(17);

		cbh0 = cbh1;

		cbh1.reset();
		cbh0.reset();
		cbh2.reset();
	}

	// RTGマネージャ初期化.
	{
		rtg_manager_.Init(&device_, 4);
	}
	
	// imgui.
	{
		if(!ngl::imgui::ImguiInterface::Instance().Initialize(&device_, swapchain_.Get()))
		{
			std::cout << "[ERROR] Initialize Imgui" << std::endl;
			assert(false);
			return false;
		}
	}
	

	// Material Shader.
	{
		constexpr char k_material_shader_file_dir[] = "../ngl/shader/material/generated";
	
		// MaterialShaderFile生成.
		//	実際は事前生成すべきだが起動時に生成.
		{
			ngl::gfx::MaterialShaderFileGenerator mtl_gen{};
			mtl_gen.GenerateMaterialShaderFiles(
				"../ngl/shader/material/impl",
				"../ngl/shader/material/pass",
				k_material_shader_file_dir);
		}
		
		// Material Shader Manager Setup.
		{
			// Material PSO Creatorを登録.
			{
				// PreZ Pass用Pso生成器.
				ngl::gfx::MaterialShaderManager::Instance().RegisterPassPsoCreator<ngl::gfx::MaterialPassPsoCreator_depth>();
				// GBuffer Pass用Pso生成器.
				ngl::gfx::MaterialShaderManager::Instance().RegisterPassPsoCreator<ngl::gfx::MaterialPassPsoCreator_gbuffer>();
				// DirectionalShadow Pass用Pso生成器.
				ngl::gfx::MaterialShaderManager::Instance().RegisterPassPsoCreator<ngl::gfx::MaterialPassPsoCreator_d_shadow>();
				
				// TODO other pass.
			}

			// Material Shader Psoセットアップ.
			ngl::gfx::MaterialShaderManager::Instance().Setup(&device_, k_material_shader_file_dir);
		}
	}

	// デフォルトテクスチャ等の簡易アクセス用クラス初期化.
	{
		if(!ngl::gfx::GlobalRenderResource::Instance().Initialize(&device_))
		{
			assert(false);
			return false;
		}
	}

	// UnorderedAccess Texture.
	{
		ngl::rhi::TextureDep::Desc desc{};
		desc.bind_flag = ngl::rhi::ResourceBindFlag::UnorderedAccess | ngl::rhi::ResourceBindFlag::ShaderResource;
		desc.format = ngl::rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
		desc.type = ngl::rhi::ETextureType::Texture2D;
		desc.width = scree_w;
		desc.height = scree_h;
		desc.initial_state = ngl::rhi::EResourceState::ShaderRead;

		tex_rw_ = new ngl::rhi::TextureDep();
		if (!tex_rw_->Initialize(&device_, desc))
		{
			std::cout << "[ERROR] Create RW Texture Initialize" << std::endl;
			assert(false);
		}
		tex_rw_srv_ = new ngl::rhi::ShaderResourceViewDep();
		if (!tex_rw_srv_->InitializeAsTexture(&device_, tex_rw_.Get(), 0, 1, 0, 1))
		{
			std::cout << "[ERROR] Create RW SRV" << std::endl;
			assert(false);
		}
		tex_rw_uav_ = new ngl::rhi::UnorderedAccessViewDep();
		if (!tex_rw_uav_->Initialize(&device_, tex_rw_.Get(), 0, 0, 1))
		{
			std::cout << "[ERROR] Create RW UAV" << std::endl;
			assert(false);
		}
	}

	// モデル読みこみ.
	{
		const char* mesh_file_stanford_bunny = "../ngl/data/model/stanford_bunny/bunny.obj";
		const char* mesh_file_spider = "../ngl/data/model/assimp/FBX/spider.fbx";
		const float spider_base_scale = 0.0001f;
		
		// シーンモデル.
#if 1
		// Sponza.
		const char* mesh_file_sponza = "../ngl/data/model/sponza_gltf/glTF/Sponza.gltf";
		const float sponza_scale = 1.0f;
		
		const char* mesh_target_scene = mesh_file_sponza;
		const float target_scene_base_scale = sponza_scale;
#else
		// Amazon Lumberyard Bistro.
		const char* mesh_file_bistro = "C:/Users/nagak/Downloads/Bistro_v5_2/Bistro_v5_2/BistroExterior.fbx";
		const float bistro_scale = 1.0f;
		
		const char* mesh_target_scene = mesh_file_bistro;
		const float target_scene_base_scale = bistro_scale;
#endif
		
		auto& ResourceMan = ngl::res::ResourceManager::Instance();
		// Mesh Component
		{
			// 基本シーンモデル読み込み.
			if(true)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);

				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_target_scene, &loaddesc));
				// スケール設定.
				mc->transform_.SetDiagonal(ngl::math::Vec3(target_scene_base_scale));
			}

			// その他モデル.
			if(true)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);
				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_file_spider, &loaddesc));
				
				ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
				tr.SetDiagonal(ngl::math::Vec4(spider_base_scale * 5.0f));
				tr.SetColumn3(ngl::math::Vec4(30.0f, 12.0f, 0.0f, 1.0f));

				mc->transform_ = ngl::math::Mat34(tr);
			}

			if(true)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);
				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_file_stanford_bunny, &loaddesc));
				
				ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
				tr.SetDiagonal(ngl::math::Vec4(1.0f));
				tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
				tr.SetColumn3(ngl::math::Vec4(0.0f, 12.0f, 0.0f, 1.0f));

				mc->transform_ = ngl::math::Mat34(tr);
			}
			if(true)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);
				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_file_stanford_bunny, &loaddesc));
				
				ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
				tr.SetDiagonal(ngl::math::Vec4(1.0f, 0.3f, 1.0f, 1.0f));//被均一スケールテスト.
				tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
				tr.SetColumn3(ngl::math::Vec4(1.5f, 12.0f, 0.0f, 1.0f));

				mc->transform_ = ngl::math::Mat34(tr);
			}
			if(true)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);
				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_file_stanford_bunny, &loaddesc));
				
				ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
				tr.SetDiagonal(ngl::math::Vec4(1.0f, 3.0f, 1.0f, 1.0f));//被均一スケールテスト.
				tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
				tr.SetColumn3(ngl::math::Vec4(3.0f, 12.0f, 0.0f, 1.0f));

				mc->transform_ = ngl::math::Mat34(tr);
			}
#if 1
			for(int i = 0; i < 100; ++i)
			{
				auto mc = std::make_shared<ngl::gfx::StaticMeshComponent>();
				mesh_comp_array_.push_back(mc);
				ngl::gfx::ResMeshData::LoadDesc loaddesc{};
				mc->Initialize(&device_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device_, mesh_file_spider, &loaddesc));

				constexpr int k_rand_f_div = 10000;
				const float randx = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
				const float randy = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
				const float randz = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
				const float randroty = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;

				constexpr float placement_range = 30.0f;

				ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
				tr.SetDiagonal(ngl::math::Vec4(spider_base_scale));
				tr = ngl::math::Mat44::RotAxisY(randroty * ngl::math::k_pi_f * 2.0f) * tr;
				tr.SetColumn3(ngl::math::Vec4(placement_range* (randx * 2.0f - 1.0f), 20.0f * randy, placement_range* (randz * 2.0f - 1.0f), 1.0f));

				mc->transform_ = ngl::math::Mat34(tr);

				// 移動テスト用.
				test_move_mesh_comp_array_.push_back(mc.get());
			}
#endif
		}
	}
	
	// Raytrace.
	//	TLAS構築時にShaderTableの最大Hitgroup数が必要な設計であるため初期化時に最大数指定する. PrimayとShadowの2種であれば 2.
	constexpr int k_system_hitgroup_count_max = 3;
	if (!rt_scene_.Initialize(&device_, k_system_hitgroup_count_max))
	{
		std::cout << "[ERROR] Initialize gfx::RtSceneManager" << std::endl;
	}
	
	// Texture Rexource読み込みのテスト.
	ngl::gfx::ResTexture::LoadDesc tex_load_desc{};
	//const char test_load_texture_file_name[] = "../ngl/data/model/sponza_gltf/glTF/6772804448157695701.jpg";
	const char test_load_texture_file_name[] = "../ngl/data/texture/sample_dds/test-dxt1.dds";
	res_texture_ = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResTexture>(&device_, test_load_texture_file_name, &tex_load_desc);
	
	ngl::time::Timer::Instance().StartTimer("app_frame_sec");
	return true;
}


void AppGame::BeginFrame()
{
	// imgui.
	ngl::imgui::ImguiInterface::Instance().BeginFrame();
}

void AppGame::SyncRender()
{	
	// RenderThread待機.
	render_thread_.Wait();
	
	
	// IMGUIのEndFrame呼び出し.
	ngl::imgui::ImguiInterface::Instance().EndFrame();


	// Graphics Deviceのフレーム準備
	device_.ReadyToNewFrame();
	
	// RTGのフレーム開始処理.
	rtg_manager_.BeginFrame();
}

// Render駆動.
void AppGame::LaunchRender()
{
	// GameThread同期部
	
	// RenderParam同期.
	SyncRenderParam();
	// RenderThreadでJob実行.
	render_thread_.Begin([this]
	{
		// RenderThreadシステム先頭処理.
		{
			// このフレーム用のシステムCommandListをRtgから取得.
			p_gfx_frame_begin_command_list_ = {};
			rtg_manager_.GetNewFrameCommandList(p_gfx_frame_begin_command_list_);
			p_gfx_frame_begin_command_list_->Begin();// begin.

			// ResourceManagerのRenderThread処理. TextureLinearBufferや MeshBufferのUploadなど.
			ngl::res::ResourceManager::Instance().UpdateResourceOnRender(&device_, p_gfx_frame_begin_command_list_);
		}

		
		// アプリケーション側のRender処理.
		std::vector<RtgGenerateCommandListSet> app_rtg_command_list_set{};
		RenderApp(app_rtg_command_list_set);
		
	
#if 0
		// システム側AsyncComputeテスト.
		ngl::rhi::ComputeCommandListDep* rtg_compute_command_list{};
		rtg_manager_.GetNewFrameCommandList(rtg_compute_command_list);
		{
			// テストのためPoolから取得したCommandListに積み込み.
			rtg_compute_command_list->Begin();
			// GraphicsQueueがComputeQueueをWaitする状況のテスト用.
			{
				auto ref_cpso = ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep>(new ngl::rhi::ComputePipelineStateDep());
				{
					ngl::rhi::ComputePipelineStateDep::Desc cpso_desc{};
					{
						ngl::gfx::ResShader::LoadDesc cs_load_desc{};
						cs_load_desc.stage = ngl::rhi::EShaderStage::Compute;
						cs_load_desc.shader_model_version = "6_3";
						cs_load_desc.entry_point_name = "main_cs";
						auto cs_load_handle = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResShader>(&device_, "../ngl/shader/test/async_task_test_cs.hlsl", &cs_load_desc);
				
						cpso_desc.cs = &cs_load_handle->data_;
					}
					// CsPso初期化.
					ref_cpso->Initialize(&device_, cpso_desc);
				}
				
				ngl::rhi::DescriptorSetDep desc_set{};
				ref_cpso->SetView(&desc_set, "rwtex_out", tex_rw_uav_.Get());
				rtg_compute_command_list->SetPipelineState(ref_cpso.Get());
				rtg_compute_command_list->SetDescriptorSet(ref_cpso.Get(), &desc_set);
				ref_cpso->DispatchHelper(rtg_compute_command_list, tex_rw_->GetWidth(), tex_rw_->GetHeight(), 1);
			}
			rtg_compute_command_list->End();
		}
	
		// フレーム先頭からAcyncComputeのテストSubmit.
		{
			ngl::rhi::CommandListBaseDep* p_command_lists[] =
			{
				rtg_compute_command_list
			};
			compute_queue_.ExecuteCommandLists(static_cast<unsigned int>(std::size(p_command_lists)), p_command_lists);
		}
#endif


		// ------------------------------------------------------------------------------------------
		const auto submit_gpu_work_id = device_.GetDeviceFrameIndex();
		const auto inflight_gpu_work_index = inflight_gpu_work_flip_;
		inflight_gpu_work_flip_ = (inflight_gpu_work_flip_ + 1) % inflight_gpu_work_flip_count_;
		// ------------------------------------------------------------------------------------------
		
		// ------------------------------------------------------------------------------------------
		// 今回フレームのコマンドをGPUにSubmitする前に前回のGPU処理完了待機.
		// GPU側にN個のキューを想定している場合は今回使用するキューのタスク完了を待つ(現状は1つ).
		if(inflight_gpu_work_id_enable_[inflight_gpu_work_index])
		{
			// 今回のGPUタスク待機バッファの完了を待機.
			gpu_wait_signal_.Wait(&gpu_wait_fence_, inflight_gpu_work_id_[inflight_gpu_work_index]);

			inflight_gpu_work_id_enable_[inflight_gpu_work_index] = false;
		}
		// ------------------------------------------------------------------------------------------
		
		// Submit to Gpu Task.
		{
			// システム用Graphics CommandList.
			{
				p_gfx_frame_begin_command_list_->End();
				ngl::rhi::CommandListBaseDep* submit_list[] = { p_gfx_frame_begin_command_list_ };
				graphics_queue_.ExecuteCommandLists(static_cast<unsigned int>(std::size(submit_list)), submit_list);
			}
		
			// RtgのCommaand.
			for(auto& e : app_rtg_command_list_set)
			{
				ngl::rtg::RenderTaskGraphBuilder::SubmitCommand(graphics_queue_, compute_queue_, e.graphics, e.compute);
			}
		}

		// Present.
		swapchain_->GetDxgiSwapChain()->Present(0, 0);
		
		// ------------------------------------------------------------------------------------------
		{
			// 今回のGPUタスクの待機用シグナル発行とそのバッファリング.
			inflight_gpu_work_id_[inflight_gpu_work_index] = submit_gpu_work_id;
			graphics_queue_.Signal(&gpu_wait_fence_, submit_gpu_work_id);

			inflight_gpu_work_id_enable_[inflight_gpu_work_index] = true;
		}
		// ------------------------------------------------------------------------------------------
		
		// フレーム描画完了.
	}
	);
	
	// RenderThreadのシングルスレッド実行モードの場合.
	if(!dbgw_enable_render_thread)
		render_thread_.Wait();
}

void AppGame::PushRenderParam(RenderParamPtr param)
{
	pushed_render_param_ = param;
}
void AppGame::SyncRenderParam()
{
	render_param_ = pushed_render_param_;
	pushed_render_param_ = {};// GameThread側クリア.

	// Game側から設定されていなかった場合はデフォルト値.
	if(!render_param_)
	{
		render_param_ = std::make_shared<RenderParam>();
	}
}

// メインループから呼ばれる
bool AppGame::Execute()
{
	// ウィンドウが無効になったら終了
	if (!window_.IsValid())
	{
		return false;
	}
	// Begin Frame.
	BeginFrame();

	
	{
		frame_sec_ = ngl::time::Timer::Instance().GetElapsedSec("app_frame_sec");
		app_sec_ += frame_sec_;

		// 再スタート
		ngl::time::Timer::Instance().StartTimer("app_frame_sec");
	}
	const float delta_sec = static_cast<float>(frame_sec_);

	
	// 操作系.
	{
		player_controller.UpdateFrame(window_, delta_sec, camera_pose_, camera_pos_);
		
		camera_pose_ = player_controller.camera_pose_;
		camera_pos_ = player_controller.camera_pos_;
	}
	
	{
		// 初期位置とサイズ.
		const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 1500, main_viewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);

		
		ImGui::Begin("Debug Window", &dbgw_test_window_enable, ImGuiWindowFlags_None);

		ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");
		ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), "[Camera Control]");
		ImGui::TextColored(ImColor(1.0f, 0.9f, 0.9f), "  Right Mouse Button + WASD + SPACE + CTRL");
		ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");
		ImGui::TextColored(ImColor(1.0f, 0.9f, 0.9f), "     (Unreal Engine Like)");
		ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");
		
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Debug Perf"))
		{
			ImGui::Text("Delta: %f [ms]", delta_sec*1000.0f);
			
			ImGui::Text("Rtg Construct: %f [ms]", dbgw_stat_primary_rtg_construct*1000.0f);
			ImGui::Text("Rtg Compile  : %f [ms]", dbgw_stat_primary_rtg_compile*1000.0f);
			ImGui::Text("Rtg Execute  : %f [ms]", dbgw_stat_primary_rtg_execute*1000.0f);

			ImGui::Separator();
			ImGui::SliderFloat("Main Thread Sleep", &dbgw_perf_main_thread_sleep_millisec, 0.0f, 100.0f);
			
			ImGui::Separator();
			ImGui::Checkbox("Enable Render Thread", &dbgw_enable_render_thread);
			ImGui::Checkbox("Enable Render Pass Parallel", &dbgw_enable_pass_render_parallel);
		}
		
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Debug View"))
		{
			ImGui::Checkbox("View GBuffer", &dbgw_view_gbuffer);
			ImGui::Checkbox("View Directional Shadow Atlas", &dbgw_view_dshadow);
			ImGui::Checkbox("View Half Dot Gray", &dbgw_view_half_dot_gray);
		}
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Directional Light"))
		{
			ImGui::SliderFloat("DirectionalLight Angle V", &dbgw_dlit_angle_v, 0.0f, ngl::math::k_pi_f*2.0f);
			ImGui::SliderFloat("DirectionalLight Angle H", &dbgw_dlit_angle_h, 0.0f, ngl::math::k_pi_f*2.0f);
		}
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Pass Setting"))
		{
			ImGui::Checkbox("Enable Feedback Blur Test", &dbgw_enable_feedback_blur_test);
			ImGui::Checkbox("Enable Raytrace Pass", &dbgw_enable_raytrace_pass);
			ImGui::Checkbox("Enable SubView Render", &dbgw_enable_sub_view_path);
		}
		
		ImGui::End();
	}
	const auto dlit_dir = ngl::math::Vec3::Normalize( ngl::math::Mat33::RotAxisY(dbgw_dlit_angle_h) * ngl::math::Mat33::RotAxisX(dbgw_dlit_angle_v) * (-ngl::math::Vec3::UnitY()));
	
	
	// オブジェクト移動.
	if(true)
	{
		for (int i = 0; i < test_move_mesh_comp_array_.size(); ++i)
		{
			auto* e = test_move_mesh_comp_array_[i];
			float move_range = (i % 10) / 10.0f;
			const float sin_curve = sinf((float)app_sec_ * 2.0f * ngl::math::k_pi_f * 0.1f * (move_range + 1.0f));

			auto trans = e->transform_.GetColumn3();
			trans.z += sin_curve * 0.075f;
			e->transform_.SetColumn3(trans);
		}
	}

	// 描画用シーン情報.
	ngl::gfx::SceneRepresentation frame_scene;
	{
		for (auto& e : mesh_comp_array_)
		{
			// Render更新.
			e->UpdateRenderData();

			// 登録.
			frame_scene.mesh_instance_array_.push_back(e.get());
		}
	}

	// RenderParamのセットアップ.
	auto new_render_param = std::make_shared<RenderParam>();
	{
		new_render_param->camera_pos = camera_pos_;
		new_render_param->camera_pose = camera_pose_;
		new_render_param->camera_fov_y = camera_fov_y;
		
		new_render_param->frame_scene = std::move(frame_scene);
		new_render_param->dlight_dir = dlit_dir;
	}
	// RenderParam送付.
	PushRenderParam(new_render_param);

	// テスト用のGameThread Sleep.
	if(0.0f < dbgw_perf_main_thread_sleep_millisec)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dbgw_perf_main_thread_sleep_millisec)));
	}

	// Render.
	{
		// Game Thread - Render Thread.
		SyncRender();
		// Render Thread.
		LaunchRender();
	}
	
	return true;
}

// アプリ側のRenderThread処理.
void AppGame::RenderApp(std::vector<RtgGenerateCommandListSet>& out_rtg_command_list_set)
{
	// フレームのSwapchainインデックス.
	const auto swapchain_index = swapchain_->GetCurrentBufferIndex();
	ngl::u32 screen_width = swapchain_->GetWidth();
	ngl::u32 screen_height = swapchain_->GetHeight();
		
	// Raytracing Scene更新.
	if(rt_scene_.IsValid())
	{
		const float screen_aspect_ratio = (float)screen_width / (float)screen_height;
		// RaytraceSceneにカメラ設定.
		rt_scene_.SetCameraInfo(
			render_param_->camera_pos,
			render_param_->camera_pose.GetColumn2(),
			render_param_->camera_pose.GetColumn1(),
			render_param_->camera_fov_y,
			screen_aspect_ratio);

		// RtScene更新. AS更新とそのCommand生成.
		rt_scene_.UpdateOnRender(&device_, p_gfx_frame_begin_command_list_, render_param_->frame_scene);
	}
	
	// SubViewの描画テスト.
	//	SubカメラでRTG描画をし, 伝搬指定した出力バッファをそのまま同一フレームのMainView描画で伝搬リソースとして利用するテスト.
	ngl::test::RenderFrameOut subview_render_frame_out {};
	if(dbgw_enable_sub_view_path)
	{
		// Pathの設定.
		ngl::test::RenderFrameDesc render_frame_desc{};
		{
			render_frame_desc.p_device = &device_;
			
			render_frame_desc.screen_w = screen_width;
			render_frame_desc.screen_h = screen_height;

			render_frame_desc.camera_pos = render_param_->camera_pos + ngl::math::Vec3(0.0f, 5.0f, 0.0f);
			render_frame_desc.camera_pose = render_param_->camera_pose * ngl::math::Mat33::RotAxisX(ngl::math::Deg2Rad(75.0f));
			render_frame_desc.camera_fov_y = render_param_->camera_fov_y;

			render_frame_desc.p_scene = &render_param_->frame_scene;
			render_frame_desc.directional_light_dir = render_param_->dlight_dir;

			{
				render_frame_desc.debug_pass_render_parallel = dbgw_enable_pass_render_parallel;
			}
			// SubViewは最低限の設定.
		}
		
		out_rtg_command_list_set.push_back({});
		RtgGenerateCommandListSet& rtg_result = out_rtg_command_list_set.back();
		// Pathの実行 (RenderTaskGraphの構築と実行).
		TestFrameRenderingPath(render_frame_desc, subview_render_frame_out, rtg_manager_, rtg_result.graphics, rtg_result.compute);
	}
	
	static ngl::rtg::RtgResourceHandle h_prev_light{};// 前回フレームハンドルのテスト.
	// MainViewの描画.
	{
		constexpr ngl::rhi::EResourceState swapchain_final_state = ngl::rhi::EResourceState::Present;// Execute後のステート指定.
		
		// Pathの設定.
		ngl::test::RenderFrameDesc render_frame_desc{};
		{
			render_frame_desc.p_device = &device_;
			
			render_frame_desc.screen_w = screen_width;
			render_frame_desc.screen_h = screen_height;

			// MainViewはSwapchain書き込みPassを動かすため情報設定.
			render_frame_desc.ref_swapchain = swapchain_;
			render_frame_desc.ref_swapchain_rtv = swapchain_rtvs_[swapchain_index];
			render_frame_desc.swapchain_state_prev = swapchain_resource_state_[swapchain_index];
			render_frame_desc.swapchain_state_next = swapchain_final_state;
			
			render_frame_desc.camera_pos = render_param_->camera_pos;
			render_frame_desc.camera_pose = render_param_->camera_pose;
			render_frame_desc.camera_fov_y = render_param_->camera_fov_y;

			render_frame_desc.p_scene = &render_param_->frame_scene;
			render_frame_desc.directional_light_dir = render_param_->dlight_dir;

			if(dbgw_enable_raytrace_pass && rt_scene_.IsValid())
			{
				// デバッグメニューからRaytracePassの有無切り替え.
				render_frame_desc.p_rt_scene = &rt_scene_;
			}
			render_frame_desc.ref_test_tex_srv = res_texture_->ref_view_;
			render_frame_desc.h_prev_lit = h_prev_light;// MainViewはヒストリ有効.
			render_frame_desc.h_other_graph_out_tex = subview_render_frame_out.h_propagate_lit;

			{
				render_frame_desc.debug_pass_render_parallel = dbgw_enable_pass_render_parallel;
				
				render_frame_desc.debugview_halfdot_gray = dbgw_view_half_dot_gray;
				render_frame_desc.debugview_enable_feedback_blur_test = dbgw_enable_feedback_blur_test;
				render_frame_desc.debugview_subview_result = dbgw_enable_sub_view_path;
				render_frame_desc.debugview_raytrace_result = dbgw_enable_raytrace_pass;

				render_frame_desc.debugview_gbuffer = dbgw_view_gbuffer;
				render_frame_desc.debugview_dshadow = dbgw_view_dshadow;
			}
		}
		
		swapchain_resource_state_[swapchain_index] = swapchain_final_state;// State変更.
		
		out_rtg_command_list_set.push_back({});
		RtgGenerateCommandListSet& rtg_result = out_rtg_command_list_set.back();
		// Pathの実行 (RenderTaskGraphの構築と実行).
		ngl::test::RenderFrameOut render_frame_out{};
		TestFrameRenderingPath(render_frame_desc, render_frame_out, rtg_manager_, rtg_result.graphics, rtg_result.compute);
		
		h_prev_light = render_frame_out.h_propagate_lit;// Rtgリソースの一部を次フレームに伝搬する.

		{
			dbgw_stat_primary_rtg_construct = render_frame_out.stat_rtg_construct_sec;
			dbgw_stat_primary_rtg_compile = render_frame_out.stat_rtg_compile_sec;
			dbgw_stat_primary_rtg_execute = render_frame_out.stat_rtg_execute_sec;
		}
	}
}



void PlayerController::UpdateFrame(ngl::platform::CoreWindow& window, float delta_sec, const ngl::math::Mat33& prev_camera_pose, const ngl::math::Vec3& prev_camera_pos)
{
	float camera_translate_speed = 10.0f;

	const auto mouse_pos = window.Dep().GetMousePosition();
	const auto mouse_pos_delta = window.Dep().GetMousePositionDelta();
	const bool mouse_l = window.Dep().GetMouseLeft();
	const bool mouse_r = window.Dep().GetMouseRight();
	const bool mouse_m = window.Dep().GetMouseMiddle();

	ngl::math::Mat33 camera_pose = prev_camera_pose;
	ngl::math::Vec3 camera_pos = prev_camera_pos;
		
	{
		const auto mx = std::get<0>(mouse_pos);
		const auto my = std::get<1>(mouse_pos);
		const ngl::math::Vec2 mouse_pos((float)mx, (float)my);

		if (!prev_mouse_r_ && mouse_r)
		{
			// MouseR Start.

			// R押下開始でマウス位置固定開始.
			window.Dep().SetMousePositionRequest(mx, my);
			window.Dep().SetMousePositionClipInWindow(true);
		}
		if (prev_mouse_r_ && !mouse_r)
		{
			// MouseR End.

			// R押下終了でマウス位置固定リセット.
			window.Dep().ResetMousePositionRequest();
			window.Dep().SetMousePositionClipInWindow(false);
		}

		// UEライクなマウスR押下中にカメラ向きと位置操作(WASD)
		// MEMO. マウスR押下中に実際のマウス位置を動かさないようにしたい(ウィンドウから出てしまうので)
		if (mouse_r)
		{
			// マウス押下中カーソル移動量(pixel)
			const ngl::math::Vec2 mouse_diff((float)std::get<0>(mouse_pos_delta), (float)std::get<1>(mouse_pos_delta));

			// 向き.
			if(true)
			{
				// 適当に回転量へ.
				const auto rot_rad = ngl::math::k_pi_f * mouse_diff * 0.001f;
				auto rot_yaw = ngl::math::Mat33::RotAxisY(rot_rad.x);
				auto rot_pitch = ngl::math::Mat33::RotAxisX(rot_rad.y);
				// 回転.
				camera_pose = camera_pose * rot_yaw * rot_pitch;

				// sideベクトルをワールドXZ麺に制限.
				if (0.9999 > std::fabsf(camera_pose.GetColumn2().y))
				{
					// 視線がY-Axisと不一致なら視線ベクトルとY-Axisから補正.
					const float sign_y = (0.0f < camera_pose.GetColumn1().y) ? 1.0f : -1.0f;
					auto lx = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitY() * sign_y, camera_pose.GetColumn2());
					auto ly = ngl::math::Vec3::Cross(camera_pose.GetColumn2(), lx);
					const auto cam_pose_transpose = ngl::math::Mat33(ngl::math::Vec3::Normalize(lx), ngl::math::Vec3::Normalize(ly), camera_pose.GetColumn2());
					camera_pose = ngl::math::Mat33::Transpose(cam_pose_transpose);
				}
				else
				{
					// 視線がY-Axisと一致か近いならサイドベクトルのY成分潰して補正.
					auto lx = camera_pose.GetColumn1();
					lx = ngl::math::Vec3({ lx.x, 0.0f, lx.z });
					auto ly = ngl::math::Vec3::Cross(camera_pose.GetColumn2(), lx);
					const auto cam_pose_transpose = ngl::math::Mat33(ngl::math::Vec3::Normalize(lx), ngl::math::Vec3::Normalize(ly), camera_pose.GetColumn2());
					camera_pose = ngl::math::Mat33::Transpose(cam_pose_transpose);
				}
			}

			// 移動.
			{
				const auto vk_a = 65;// VK_A.
				if (window.Dep().GetVirtualKeyState()[VK_SPACE])
				{
					camera_pos += camera_pose.GetColumn1() * delta_sec * camera_translate_speed;
				}
				if (window.Dep().GetVirtualKeyState()[VK_CONTROL])
				{
					camera_pos += -camera_pose.GetColumn1() * delta_sec * camera_translate_speed;
				}
				if (window.Dep().GetVirtualKeyState()[vk_a + 'w' - 'a'])
				{
					camera_pos += camera_pose.GetColumn2() * delta_sec * camera_translate_speed;
				}
				if (window.Dep().GetVirtualKeyState()[vk_a + 's' - 'a'])
				{
					camera_pos += -camera_pose.GetColumn2() * delta_sec * camera_translate_speed;
				}
				if (window.Dep().GetVirtualKeyState()[vk_a + 'd' - 'a'])
				{
					camera_pos += camera_pose.GetColumn0() * delta_sec * camera_translate_speed;
				}
				if (window.Dep().GetVirtualKeyState()[vk_a + 'a' - 'a'])
				{
					camera_pos += -camera_pose.GetColumn0() * delta_sec * camera_translate_speed;
				}
			}
		}
		prev_mouse_r_ = mouse_r;
	}

	camera_pose_ = camera_pose;
	camera_pos_ = camera_pos;
}






int main()
{
    std::cout << "Boot App" << std::endl;
	ngl::time::Timer::Instance().StartTimer("AppGameTime");

	{
		std::unique_ptr<ngl::boot::BootApplication> boot(ngl::boot::BootApplication::Create());
		AppGame app;
		boot->Run(&app);
	}

	std::cout << "App Time: " << ngl::time::Timer::Instance().GetElapsedSec("AppGameTime") << std::endl;
	return 0;
}

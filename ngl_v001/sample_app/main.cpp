
#include <array>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "boot/boot_application.h"
#include "file/file.h"
#include "math/math.h"
#include "platform/window.h"
#include "thread/test_lockfree_stack.h"
#include "util/bit_operation.h"
#include "util/time/timer.h"

// resource
#include "resource/resource_manager.h"

// rhi
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

// GraphicsFramework.
#include "framework/gfx_framework.h"

// gfx
#include "gfx/game_scene.h"
#include "gfx/raytrace_scene.h"
#include "render/scene/scene_mesh.h"
#include "render/scene/scene_skybox.h"

// マテリアルシェーダ関連.
#include "gfx/material/material_shader_generator.h"
#include "gfx/material/material_shader_manager.h"

// Render Path
#include "render/test_render_path.h"

#include "render/app/sw_tess/sw_tessellation_mesh.h"
#include "render/app/ssvg/ssvg.h"


// imguiのシステム処理Wrapper.
#include "imgui/imgui_interface.h"
// ImGui.
static bool dbgw_test_window_enable               = true;
static bool dbgw_render_thread                    = true;
static bool dbgw_multithread_render_pass          = true;
static bool dbgw_multithread_cascade_shadow       = true;
static float dbgw_perf_main_thread_sleep_millisec = 0.0f;
static bool dbgw_enable_feedback_blur_test        = true;
static bool dbgw_enable_sub_view_path             = false;
static bool dbgw_enable_raytrace_pass             = false;
static bool dbgw_view_half_dot_gray               = false;
static bool dbgw_view_gbuffer                     = false;
static bool dbgw_view_dshadow                     = false;

//static bool dbgw_view_ssvg_voxel                  = true;
static float dbgw_view_ssvg_voxel_rate            = 0.5f;
static bool dbgw_view_ssvg_sky_visibility         = false;
static bool dbgw_enable_gi_lighting                = true;

static float dbgw_dlit_angle_v                    = 0.4f;
static float dbgw_dlit_angle_h                    = 4.1f;
static float dbgw_camera_speed                    = 5.0f;//10.0f;

static float dbgw_stat_primary_rtg_construct = {};
static float dbgw_stat_primary_rtg_compile   = {};
static float dbgw_stat_primary_rtg_execute   = {};

static int dbgw_sky_debug_mode       = {};
static float dbgw_sky_debug_mip_bias = 0.0f;

static float sw_tess_important_point_offset_in_view = 7.0;

static int sw_tess_fixed_subdivision_level = -1; // -1で無効、0以上で固定分割レベルを指定
static bool sw_tess_update_tessellation = true; // trueでテッセレーション更新を有効化
static bool sw_tess_update_tessellation_frame_toggle = false; // trueで1F毎にテッセレーション更新フラグをOFFにするデバッグ用.
static float sw_tess_split_threshold = 0.5f; // テッセレーション分割閾値

static int sw_tess_debug_bisector_id = -1;      // デバッグ対象BisectorID（-1で無効）
static int sw_tess_debug_bisector_depth = -1;   // デバッグ対象BisectorDepth（-1で無効）
static int sw_tess_debug_bisector_neighbor = -1;

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
    // AppのMainThread処理.
    bool ExecuteApp();
    // AppのRenderThread処理.
    void RenderApp(ngl::fwk::RtgFrameRenderSubmitCommandBuffer& out_rtg_command_list_set);

private:
    struct RenderParam
    {
        ngl::math::Vec3 camera_pos   = {0.0f, 0.0f, 0.0f};
        ngl::math::Mat33 camera_pose = ngl::math::Mat33::Identity();
        float camera_fov_y           = ngl::math::Deg2Rad(60.0f);  // not half fov.

        ngl::gfx::SceneRepresentation frame_scene{};
        ngl::math::Vec3 dlight_dir{};

        // any.
    };
    using RenderParamPtr = std::shared_ptr<RenderParam>;
    RenderParamPtr pushed_render_param_{};  // RenderThreadでへ送付するための一時データ.
    RenderParamPtr render_param_{};         // RenderThreadで読み取り可能なRenderParam.
    void PushRenderParam(RenderParamPtr param);
    void SyncRenderParam();

private:
    double app_sec_   = 0.0f;
    double frame_sec_ = 0.0f;

    double moving_avg_t50_frame_sec_ = 0.0f;

    ngl::platform::CoreWindow window_;

    // Graphicsフレームワーク.
    ngl::fwk::GraphicsFramework gfxfw_{};
    std::vector<ngl::rhi::EResourceState> swapchain_resource_state_;

    //ngl::math::Vec3 camera_pos_   = {0.368f, 1.237f, 0.453f};//{0.0f, 2.0f, -5.0f};
    ngl::math::Vec3 camera_pos_   = {1.871f, 1.347f, 1.399f};//{0.0f, 2.0f, -5.0f};
    ngl::math::Mat33 camera_pose_ = ngl::math::Mat33::RotAxisY(ngl::math::Deg2Rad(-90.0f));// ngl::math::Mat33::Identity();
    float camera_fov_y            = ngl::math::Deg2Rad(60.0f);  // not half fov.
    PlayerController player_controller{};

    // GfxSceneMesh版
    std::vector<std::shared_ptr<ngl::gfx::scene::SceneMesh>> mesh_entity_array_;
    std::vector<ngl::gfx::scene::SceneMesh*> test_move_mesh_entity_array_;
    
    // SwTessellationMesh管理用
    std::vector<ngl::render::app::SwTessellationMesh*> sw_tessellation_mesh_array_;

    // RaytraceScene.
    ngl::gfx::RtSceneManager rt_scene_;

    ngl::render::app::SsVg  ssvg_;

    ngl::fwk::GfxScene gfx_scene_{};
    ngl::gfx::scene::SceneSkyBox skybox_{};

    ngl::rhi::RefTextureDep tex_rw_;
    ngl::rhi::RefSrvDep tex_rw_srv_;
    ngl::rhi::RefUavDep tex_rw_uav_;

    // Loaded Texture.
    ngl::res::ResourceHandle<ngl::gfx::ResTexture> res_texture_{};
};

// -----------------------------------------------
// テストコードの呼び出しはここに書く.
static void TestEntry()
{
    // テストコード呼び出し.
    ngl::thread::TestLockFreeStackIntrusive();
    ngl::thread::TestFixedSizeLockFreeStack();
    ngl::thread::TestStaticSizeLockFreeStack();


    ngl::render::app::ConcurrentBinaryTreeU32::Test();
}

AppGame::AppGame()
{
    TestEntry();
}
AppGame::~AppGame()
{
    // Graphicsフレームワークのジョブ系終了.
    gfxfw_.FinalizePrev();
    {
        // RenderParam内の各種参照をクリア.
        {
            PushRenderParam({});
            SyncRenderParam();
        }

        skybox_.FinalizeGfx();

        rt_scene_ = {};
        ssvg_.Finalize();

        // リソース参照クリア.
        mesh_entity_array_.clear();
        sw_tessellation_mesh_array_.clear();

        // Material Shader Manager.
        ngl::gfx::MaterialShaderManager::Instance().Finalize();
    }
    // Graphicsフレームワークのリソース系終了.
    gfxfw_.FinalizePost();
}

bool AppGame::Initialize()
{
    constexpr auto scree_h = 1080;
    constexpr auto scree_w = scree_h * 16 / 9;

    // ウィンドウ作成
    if (!window_.Initialize(_T("ToyRenderer"), scree_w, scree_h))
    {
        return false;
    }
    // グラフィックスフレームワーク初期化.
    if (!gfxfw_.Initialize(&window_))
    {
        assert(false && u8"Failed Initialize Rendering Framework.");
    }
    // グラフィックスフレームワークからDevice取得.
    auto& device = gfxfw_.device_;
    // Swapchainバッファのステート管理は現状App側で行う.
    swapchain_resource_state_.resize(gfxfw_.swapchain_->NumResource());
    for (auto i = 0u; i < gfxfw_.swapchain_->NumResource(); ++i)
    {
        swapchain_resource_state_[i] = gfxfw_.GetSwapchainBufferInitialState();
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
            ngl::gfx::MaterialShaderManager::Instance().Setup(&device, k_material_shader_file_dir);
        }
    }

    // UnorderedAccess Texture.
    {
        ngl::rhi::TextureDep::Desc desc{};
        desc.bind_flag     = ngl::rhi::ResourceBindFlag::UnorderedAccess | ngl::rhi::ResourceBindFlag::ShaderResource;
        desc.format        = ngl::rhi::EResourceFormat::Format_R16G16B16A16_FLOAT;
        desc.type          = ngl::rhi::ETextureType::Texture2D;
        desc.width         = scree_w;
        desc.height        = scree_h;
        desc.initial_state = ngl::rhi::EResourceState::ShaderRead;

        tex_rw_.Reset(new ngl::rhi::TextureDep());
        if (!tex_rw_->Initialize(&device, desc))
        {
            std::cout << "[ERROR] Create RW Texture Initialize" << std::endl;
            assert(false);
        }
        tex_rw_srv_.Reset(new ngl::rhi::ShaderResourceViewDep());
        if (!tex_rw_srv_->InitializeAsTexture(&device, tex_rw_.Get(), 0, 1, 0, 1))
        {
            std::cout << "[ERROR] Create RW SRV" << std::endl;
            assert(false);
        }
        tex_rw_uav_.Reset(new ngl::rhi::UnorderedAccessViewDep());
        if (!tex_rw_uav_->InitializeRwTexture(&device, tex_rw_.Get(), 0, 0, 1))
        {
            std::cout << "[ERROR] Create RW UAV" << std::endl;
            assert(false);
        }
    }

    // GfxScene初期化.
    {
        gfx_scene_.buffer_skybox_.Initialize(128);
        gfx_scene_.buffer_mesh_.Initialize(65536);
    }

    constexpr char path_sky_hdr_panorama_pisa[]         = "../ngl/data/texture/vgl/pisa/pisa.hdr";
    constexpr char path_sky_hdr_panorama_ennis[]        = "../ngl/data/texture/vgl/ennis/ennis.hdr";
    constexpr char path_sky_hdr_panorama_grace_new[]    = "../ngl/data/texture/vgl/grace-new/grace-new.hdr";
    constexpr char path_sky_hdr_panorama_uffizi_large[] = "../ngl/data/texture/vgl/uffizi-large/uffizi-large.hdr";

    const auto* path_sky_panorama = path_sky_hdr_panorama_pisa;
    // const auto* path_sky_panorama = path_sky_hdr_panorama_ennis;
    // const auto* path_sky_panorama = path_sky_hdr_panorama_grace_new;  // 高周波.
    // const auto* path_sky_panorama = path_sky_hdr_panorama_uffizi_large;
    skybox_.InitializeGfx(&gfx_scene_);
    if (!skybox_.SetupAsPanorama(&device, path_sky_panorama))
    {
        std::cout << "[ERROR] Initialize SceneSkyBox" << std::endl;
    }

    // モデル読みこみ.
    {
        const char* mesh_file_stanford_bunny = "../ngl/data/model/stanford_bunny/bunny.obj";
        const char* mesh_file_spider         = "../ngl/data/model/assimp/FBX/spider.fbx";
        const float spider_base_scale        = 0.0001f;

        const char* mesh_file_box = "K:\\GitHub\\sample_projct_lib\\ngl_v001\\ngl\\external\\assimp\\test\\models\\FBX\\box.fbx";

        // シーンモデル.
#if 1
        // Sponza.
        const char* mesh_file_sponza = "../ngl/data/model/sponza_gltf/glTF/Sponza.gltf";
        const float sponza_scale     = 1.0f;

        const char* mesh_target_scene       = mesh_file_sponza;
        const float target_scene_base_scale = sponza_scale;
#else
        // Amazon Lumberyard Bistro.
        const char* mesh_file_bistro = "C:/Users/nagak/Downloads/Bistro_v5_2/Bistro_v5_2/BistroExterior.fbx";
        const float bistro_scale     = 1.0f;

        const char* mesh_target_scene       = mesh_file_bistro;
        const float target_scene_base_scale = bistro_scale;
#endif

        std::shared_ptr<ngl::gfx::MeshData> procedural_mesh_data = std::make_shared<ngl::gfx::MeshData>();
        {
            const float mesh_scale = 10.0f;
            ngl::math::Vec3 quad_pos[4] = {
                ngl::math::Vec3(-1.0f, 0.0f, -1.0f) * mesh_scale,
                ngl::math::Vec3(-1.0f, 0.0f, 1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, 1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, -1.0f) * mesh_scale
            };
            ngl::math::Vec3 quad_normal[4] = {
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f)
            };
            ngl::math::Vec3 quad_tangent[4] = {
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f)
            };
            ngl::math::Vec3 quad_binormal[4] = {
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f)
            };
            ngl::math::Vec2 quad_texcoord[4] = {
                ngl::math::Vec2(0.0f, 0.0f),
                ngl::math::Vec2(1.0f, 0.0f),
                ngl::math::Vec2(0.0f, 1.0f),
                ngl::math::Vec2(1.0f, 1.0f)
            };
            ngl::gfx::VertexColor quad_color[4] = {
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255}
            };

            ngl::u32 index_data[6] = {
                0, 1, 2,
                0, 2, 3
            };

            ngl::gfx::MeshShapeInitializeSourceData init_source_data{};
            init_source_data.num_vertex_ = 4;
            init_source_data.num_primitive_ = 2;
            init_source_data.index_ = index_data;
            init_source_data.position_ = quad_pos;
            init_source_data.normal_ = quad_normal;
            init_source_data.tangent_ = quad_tangent;
            init_source_data.binormal_ = quad_binormal;
            init_source_data.texcoord_.push_back(quad_texcoord);
            init_source_data.color_.push_back(quad_color);

            // MeshData生成.
            GenerateMeshDataProcedural(*procedural_mesh_data, &device, init_source_data);
        }


        auto& ResourceMan = ngl::res::ResourceManager::Instance();
        // SceneMesh.
        {
#if 1
            if (true)
            {
                // メイン背景.
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);

                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_target_scene, &loaddesc));
                // スケール設定.
                ngl::math::Mat34 tr = ngl::math::Mat34::Identity();
                tr.SetDiagonal(ngl::math::Vec3(target_scene_base_scale));
                mc->SetTransform(tr);
            }

            // その他モデル.
            if (0)
            {
                // クモ
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc));

                ngl::math::Mat34 tr = ngl::math::Mat34::Identity();
                tr.SetDiagonal(ngl::math::Vec3(spider_base_scale * 4.0f));
                //tr.SetColumn3(ngl::math::Vec4(30.0f, 12.0f, 0.0f, 1.0f));
                tr.SetColumn3(ngl::math::Vec3(-10.0f, 15.0f, 4.0f));

                mc->SetTransform(ngl::math::Mat34(tr));
            }

            if (0)
            {
                // ウサギ
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_stanford_bunny, &loaddesc));

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                tr.SetDiagonal(ngl::math::Vec4(1.0f));
                // tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
                tr.SetColumn3(ngl::math::Vec4(0.0f, 12.0f, 0.0f, 1.0f));

                mc->SetTransform(ngl::math::Mat34(tr));
            }
            if (0)
            {
                // ウサギ
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_stanford_bunny, &loaddesc));

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                tr.SetDiagonal(ngl::math::Vec4(1.0f, 0.3f, 1.0f, 1.0f));  // 被均一スケールテスト.
                tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
                tr.SetColumn3(ngl::math::Vec4(2.5f, 12.0f, 0.0f, 1.0f));

                mc->SetTransform(ngl::math::Mat34(tr));
            }
            if (0)
            {
                // ウサギ
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_stanford_bunny, &loaddesc));

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                tr.SetDiagonal(ngl::math::Vec4(1.0f, 3.0f, 1.0f, 1.0f));  // 被均一スケールテスト.
                tr = ngl::math::Mat44::RotAxisX(0.1f * ngl::math::k_pi_f * 2.0f) * tr;
                tr.SetColumn3(ngl::math::Vec4(3.0f, 12.0f, 0.0f, 1.0f));

                mc->SetTransform(ngl::math::Mat34(tr));
            }
#endif

#if 1
            // 適当にたくさんモデル生成.
            for (int i = 0; i < 50; ++i)
            {
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc));

                constexpr int k_rand_f_div = 10000;
                const float randx          = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
                const float randy          = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
                const float randz          = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;
                const float randroty       = (std::rand() % k_rand_f_div) / (float)k_rand_f_div;

                constexpr float placement_range = 30.0f;

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                
                tr.SetDiagonal(ngl::math::Vec4(spider_base_scale * 4.0f));

                tr = ngl::math::Mat44::RotAxisY(randroty * ngl::math::k_pi_f * 2.0f) * tr;
                tr.SetColumn3(ngl::math::Vec4(placement_range * (randx * 2.0f - 1.0f), 20.0f * randy, placement_range * (randz * 2.0f - 1.0f), 1.0f));

                mc->SetTransform(ngl::math::Mat34(tr));

                // 移動テスト用.
                test_move_mesh_entity_array_.push_back(mc.get());
            }
#endif

            // SwTessellationのテスト.
            if (0)
            {
                auto mc = std::make_shared<ngl::render::app::SwTessellationMesh>();
                mesh_entity_array_.push_back(mc);
                
                // SwTessellationMesh管理用vectorにも追加
                sw_tessellation_mesh_array_.push_back(mc.get());

                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                #if 0
                // 蜘蛛
                constexpr int tessellation_level = 4;  // 0で無効、1以上で有効.
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc), {}, tessellation_level);
                tr.SetDiagonal(ngl::math::Vec4(spider_base_scale * 3.0f, 1.0f));
                #else
                constexpr int tessellation_level = 9;
                    #if 1
                        mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_box, &loaddesc), procedural_mesh_data, tessellation_level);
                    #else
                        mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_box, &loaddesc), {}, tessellation_level);
                        tr.SetDiagonal(ngl::math::Vec4(60.0f));
                        tr = ngl::math::Mat44::RotAxisY(ngl::math::k_pi_f * 0.1f) * ngl::math::Mat44::RotAxisZ(ngl::math::k_pi_f * -0.15f) * ngl::math::Mat44::RotAxisX(ngl::math::k_pi_f * 0.65f) * tr;
                    #endif
                #endif
                tr.SetColumn3(ngl::math::Vec4(0.0f, 10.0f, 0.0f, 0.0f));

                mc->SetTransform(ngl::math::Mat34(tr));
            }

            // 単純形状テスト.
            if(0)
            {
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                
                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_box, &loaddesc), procedural_mesh_data);

                //tr.SetColumn3(ngl::math::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
                //tr.SetColumn3(ngl::math::Vec4(0.0f, 0.5f, 0.5f, 0.0f));
                
                mc->SetTransform(ngl::math::Mat34(tr));
            }


        }
    }

    #if 0
        // Raytrace. 初期化しなければ描画パスでも処理されなくなる.
        //	TLAS構築時にShaderTableの最大Hitgroup数が必要な設計であるため初期化時に最大数指定する. PrimayとShadowの2種であれば 2.
        constexpr int k_system_hitgroup_count_max = 3;
        if (!rt_scene_.Initialize(&device, k_system_hitgroup_count_max))
        {
            std::cout << "[ERROR] Initialize gfx::RtSceneManager" << std::endl;
        }
    #endif
    
    // SSVG.
    ssvg_.Initialize(&device, ngl::math::Vec3u(64), 3.0f, ngl::math::Vec3u(32), 2.0f);
    //ngl::render::app::SsVg::dbg_view_mode_ = -1;



    // Texture Rexource読み込みのテスト.
    ngl::gfx::ResTexture::LoadDesc tex_load_desc{};
    // const char test_load_texture_file_name[] = "../ngl/data/model/sponza_gltf/glTF/6772804448157695701.jpg";
    const char test_load_texture_file_name[] = "../ngl/data/texture/sample_dds/test-dxt1.dds";
    // const char test_load_texture_file_name[] = "../ngl/data/texture/vgl/pisa/pisa.hdr";
    res_texture_ = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResTexture>(&device, test_load_texture_file_name, &tex_load_desc);

    ngl::time::Timer::Instance().StartTimer("app_frame_sec");
    return true;
}

void AppGame::BeginFrame()
{
    // フレームワークのフレーム開始タイミング処理.
    gfxfw_.BeginFrame();
}

void AppGame::SyncRender()
{
    // フレームワークのRender同期タイミング処理.
    gfxfw_.SyncRender();
}

// Render駆動.
void AppGame::LaunchRender()
{
    // RenderParam同期.
    SyncRenderParam();

    // フレームワークのRenderThreadにAppの描画処理実行を依頼.
    gfxfw_.BeginFrameRender([this](ngl::fwk::RtgFrameRenderSubmitCommandBuffer& app_rtg_command_list_set)
                            {
		// アプリケーション側のRender処理.
		RenderApp(app_rtg_command_list_set); });

    // RenderThread強制待機デバッグ.
    if (!dbgw_render_thread)
        gfxfw_.ForceWaitFrameRender();
}

void AppGame::PushRenderParam(RenderParamPtr param)
{
    pushed_render_param_ = param;
}
void AppGame::SyncRenderParam()
{
    render_param_        = pushed_render_param_;
    pushed_render_param_ = {};  // GameThread側クリア.

    // Game側から設定されていなかった場合はデフォルト値.
    if (!render_param_)
    {
        render_param_ = std::make_shared<RenderParam>();
    }
}

// メインループから呼ばれる
bool AppGame::Execute()
{
    // ウィンドウが無効になったら終了
    if (!gfxfw_.IsValid())
    {
        return false;
    }
    // Begin Frame.
    BeginFrame();

    // App側MainThread処理.
    if (!ExecuteApp())
    {
        return false;
    }

    // Sync MainThread-RenderThread.
    SyncRender();

    // Launch Render Thread.
    LaunchRender();

    return true;
}
// App側MainThread処理.
bool AppGame::ExecuteApp()
{
    {
        frame_sec_ = ngl::time::Timer::Instance().GetElapsedSec("app_frame_sec");
        ngl::time::Timer::Instance().StartTimer("app_frame_sec");  // リスタート.
        app_sec_ += frame_sec_;

        moving_avg_t50_frame_sec_ = (0.5) * frame_sec_ + (1.0 - 0.5) * moving_avg_t50_frame_sec_;
    }
    const float delta_sec = static_cast<float>(frame_sec_);

    // 操作系.
    {
        player_controller.UpdateFrame(window_, delta_sec, camera_pose_, camera_pos_);

        camera_pose_ = player_controller.camera_pose_;
        camera_pos_  = player_controller.camera_pos_;
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

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("View Info"))
        {
            ImGui::Text("Camera Dir:			%.3f, %.3f, %.3f", camera_pose_.GetColumn2().x, camera_pose_.GetColumn2().y, camera_pose_.GetColumn2().z);
            ImGui::Text("Camera Pos:			%.3f, %.3f, %.3f", camera_pos_.x, camera_pos_.y, camera_pos_.z );

            ImGui::SliderFloat("Camera Speed", &dbgw_camera_speed, 0.5f, 100.0f);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Debug Perf"))
        {
            ImGui::Text("Delta:			%f [ms]", delta_sec * 1000.0f);
            ImGui::Text("Delta(avg0.5):	%f [ms]", moving_avg_t50_frame_sec_ * 1000.0f);

            const auto prev_frame_gfx_stat = gfxfw_.GetStatistics(1);
            ImGui::Text("App Render         : %f [ms]", static_cast<double>(prev_frame_gfx_stat.app_render_func_micro_sec) / (1000.0));
            ImGui::Text("Wait RenderThread  : %f [ms]", static_cast<double>(prev_frame_gfx_stat.wait_render_thread_micro_sec) / (1000.0));
            ImGui::Text("Wait Gpu           : %f [ms]", static_cast<double>(prev_frame_gfx_stat.wait_gpu_fence_micro_sec) / (1000.0));
            ImGui::Text("Present Cpu Block  : %f [ms]", static_cast<double>(prev_frame_gfx_stat.wait_present_micro_sec) / (1000.0));

            ImGui::Text("Rtg Construct: %f [ms]", dbgw_stat_primary_rtg_construct * 1000.0f);
            ImGui::Text("Rtg Compile  : %f [ms]", dbgw_stat_primary_rtg_compile * 1000.0f);
            ImGui::Text("Rtg Execute  : %f [ms]", dbgw_stat_primary_rtg_execute * 1000.0f);

            ImGui::Separator();
            ImGui::SliderFloat("Main Thread Sleep Test [ms]", &dbgw_perf_main_thread_sleep_millisec, 0.0f, 100.0f);

            ImGui::Separator();
            ImGui::Checkbox("Enable Render Thread", &dbgw_render_thread);
            ImGui::Checkbox("Enable MultiThread RenderPass", &dbgw_multithread_render_pass);
            ImGui::Checkbox("Enable MultiThread CascadeShadow", &dbgw_multithread_cascade_shadow);
        }
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("RHI"))
        {
            const auto free_dynamic_descriptor_count = gfxfw_.device_.GeDynamicDescriptorManager()->GetFreeDescriptorCount();
            const auto max_dynamic_descriptor_count  = gfxfw_.device_.GeDynamicDescriptorManager()->GetMaxDescriptorCount();
            // Dynamic Descriptorの残量.
            ImGui::Text("DynamicDescriptor Free Count : %d / %d (%.2f)",
                        free_dynamic_descriptor_count, max_dynamic_descriptor_count, 100.0f * (float)free_dynamic_descriptor_count / (float)max_dynamic_descriptor_count);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Debug View"))
        {
            ImGui::Checkbox("View GBuffer", &dbgw_view_gbuffer);
            ImGui::Checkbox("View Directional Shadow Atlas", &dbgw_view_dshadow);
            ImGui::Checkbox("View Half Dot Gray", &dbgw_view_half_dot_gray);
            // sky visibilityデバッグ.
            ImGui::Checkbox("View Ssvg Sky Visibility", &dbgw_view_ssvg_sky_visibility);
            ImGui::Checkbox("Enable GI Lighting", &dbgw_enable_gi_lighting);
        }
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Directional Light"))
        {
            ImGui::SliderFloat("DirectionalLight Angle V", &dbgw_dlit_angle_v, 0.0f, ngl::math::k_pi_f * 2.0f);
            ImGui::SliderFloat("DirectionalLight Angle H", &dbgw_dlit_angle_h, 0.0f, ngl::math::k_pi_f * 2.0f);
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Sky"))
        {
            if (ImGui::CollapsingHeader("IBL"))
            {
                bool param_prevent_aliasing_mode_diffuse  = skybox_.GetParam_PreventAliasingModeDiffuse();
                bool param_prevent_aliasing_mode_specular = skybox_.GetParam_PreventAliasingModeSpecular();

                ImGui::Checkbox("prevent aliasing mode diffuse", &param_prevent_aliasing_mode_diffuse);
                ImGui::Checkbox("prevent aliasing mode specular", &param_prevent_aliasing_mode_specular);

                skybox_.SetParam_PreventAliasingModeDiffuse(param_prevent_aliasing_mode_diffuse);
                skybox_.SetParam_PreventAliasingModeSpecular(param_prevent_aliasing_mode_specular);

                if (ImGui::Button("recalculate"))
                {
                    skybox_.RecalculateIblTexture();
                }
            }

            ImGui::SliderInt("sky debug mode", &dbgw_sky_debug_mode, 0, static_cast<int>(ngl::gfx::SceneRepresentation::EDebugMode::_MAX) - 1);
            ImGui::SliderFloat("sky debug mip bias", &dbgw_sky_debug_mip_bias, 0.0f, 12.0f);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Pass Setting"))
        {
            ImGui::Checkbox("Enable Feedback Blur Test", &dbgw_enable_feedback_blur_test);
            ImGui::Checkbox("Enable Raytrace Pass", &dbgw_enable_raytrace_pass);
            ImGui::Checkbox("Enable SubView Render", &dbgw_enable_sub_view_path);
        }


        
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("SwTessellation Mesh"))
        {
            ImGui::Checkbox("Enable Tessellation Update", &sw_tess_update_tessellation);
            ImGui::Checkbox("Tessellation Update Frame Toggle", &sw_tess_update_tessellation_frame_toggle);
            ImGui::SliderInt("Fixed Subdivision Level", &sw_tess_fixed_subdivision_level, -1, 10);
            ImGui::SliderFloat("Split Threshold", &sw_tess_split_threshold, 0.1f, 1.5f, "%.4f");

            if (ImGui::Button("Reset Tessellation"))
            {
                for (auto* sw_tess_mesh : sw_tessellation_mesh_array_)
                {
                    sw_tess_mesh->ResetTessellation();
                }
            }

            ImGui::Separator();
            ImGui::SliderFloat("Important Point View Offset", &sw_tess_important_point_offset_in_view, 0.01f, 50.0f);

            ImGui::Separator();
            ImGui::Text("Debug Target Bisector:");
            ImGui::SliderInt("Bisector Depth", &sw_tess_debug_bisector_depth, -1, 15);
            ImGui::InputInt("Bisector ID", &sw_tess_debug_bisector_id, 1);
            
            ImGui::SliderInt("Neighbor(Twin,Prev,Next)", &sw_tess_debug_bisector_neighbor, -1, 2);

            if (ImGui::Button("Clear Debug Target"))
            {
                sw_tess_debug_bisector_id = -1;
                sw_tess_debug_bisector_depth = -1;
                sw_tess_debug_bisector_neighbor = -1;
            }
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Ssvg"))
        {
            if (ImGui::CollapsingHeader("Voxel Debug"))
            {
                ImGui::SliderInt("View Mode", &ngl::render::app::SsVg::dbg_view_mode_, -1, 10);
                ImGui::SliderFloat("Visualize Screen Rate", &dbgw_view_ssvg_voxel_rate, 0.0f, 1.0f);
            }

            if (ImGui::CollapsingHeader("Probe Debug"))
            {
                ImGui::SliderInt("Wcp Probe Mode", &ngl::render::app::SsVg::dbg_wcp_probe_debug_mode_, -1, 10);
                ImGui::SliderInt("Bbv Probe Mode", &ngl::render::app::SsVg::dbg_bbv_probe_debug_mode_, -1, 10);
                
                ImGui::SliderFloat("Probe Scale", &ngl::render::app::SsVg::dbg_probe_scale_, 0.01f, 10.0f);
                ImGui::SliderFloat("Probe Near Geometry Scale", &ngl::render::app::SsVg::dbg_probe_near_geom_scale_, 0.01f, 10.0f);
            }

        }

        ImGui::End();
    }
    const auto dlit_dir = ngl::math::Vec3::Normalize(ngl::math::Mat33::RotAxisY(dbgw_dlit_angle_h) * ngl::math::Mat33::RotAxisX(dbgw_dlit_angle_v) * (-ngl::math::Vec3::UnitY()));

    // オブジェクト移動.
    if (true)
    {
        for (int i = 0; i < test_move_mesh_entity_array_.size(); ++i)
        {
            auto* e               = test_move_mesh_entity_array_[i];
            if (nullptr == e)
                continue;

            float move_range      = (i % 10) / 10.0f;
            const float sin_curve = sinf((float)app_sec_ * 2.0f * ngl::math::k_pi_f * 0.1f * (move_range + 1.0f));

            auto tr    = e->GetTransform();
            auto trans = tr.GetColumn3();
            trans.z += sin_curve * delta_sec * 3.0f;
            tr.SetColumn3(trans);
            e->SetTransform(tr);
        }
    }

    // CBTテッセレーションメッシュに情報設定.
    const ngl::math::Vec3 tessellation_important_point = camera_pos_ + camera_pose_.GetColumn2() * sw_tess_important_point_offset_in_view;
    for (auto* sw_tess_mesh : sw_tessellation_mesh_array_)
    {
        sw_tess_mesh->SetImportantPoint(tessellation_important_point);

        // デバッグ用
        sw_tess_mesh->SetFixedSubdivisionLevel(sw_tess_fixed_subdivision_level);
        sw_tess_mesh->SetDebugTargetBisector(sw_tess_debug_bisector_id, sw_tess_debug_bisector_depth);
        sw_tess_mesh->SetDebugBisectorNeighbor(sw_tess_debug_bisector_neighbor);

        // テッセレーション分割閾値を設定
        sw_tess_mesh->SetTessellationSplitThreshold(sw_tess_split_threshold);

        sw_tess_mesh->SetTessellationUpdate(sw_tess_update_tessellation);
    }

    if(sw_tess_update_tessellation_frame_toggle)
    {
        // 1FでOFFにする.
        sw_tess_update_tessellation = false;
    }

    // 描画用シーン情報.
    ngl::gfx::SceneRepresentation frame_scene;
    {
        for (auto& e : mesh_entity_array_)
        {
            if(nullptr == e.get())
                continue;

            // Render更新.
            e->UpdateForRender();

            // 登録.
            frame_scene.mesh_proxy_id_array_.push_back(e->GetMeshProxyId());
        }

        // GfxScene.
        frame_scene.gfx_scene_ = &gfx_scene_;

        // sky.
        {
            // GfxSceneComponentの仕組みでSkyBox情報を取り扱うテスト
            skybox_.UpdateForRender();
            frame_scene.skybox_proxy_id_ = skybox_.GetSkyBoxProxyId();

            frame_scene.sky_debug_mode_     = static_cast<ngl::gfx::SceneRepresentation::EDebugMode>(dbgw_sky_debug_mode);
            frame_scene.sky_debug_mip_bias_ = dbgw_sky_debug_mip_bias;
        }
    }

    // RenderParamのセットアップ.
    auto new_render_param = std::make_shared<RenderParam>();
    {
        new_render_param->camera_pos   = camera_pos_;
        new_render_param->camera_pose  = camera_pose_;
        new_render_param->camera_fov_y = camera_fov_y;

        new_render_param->frame_scene = std::move(frame_scene);
        new_render_param->dlight_dir  = dlit_dir;
    }
    // RenderParam送付.
    PushRenderParam(new_render_param);

    // テスト用のGameThread Sleep.
    if (0.0f < dbgw_perf_main_thread_sleep_millisec)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dbgw_perf_main_thread_sleep_millisec)));
    }

    return true;
}

// アプリ側のRenderThread処理.
void AppGame::RenderApp(ngl::fwk::RtgFrameRenderSubmitCommandBuffer& out_rtg_command_list_set)
{
    // フレームのSwapchainインデックス.
    const auto swapchain_index = gfxfw_.swapchain_->GetCurrentBufferIndex();
    ngl::u32 screen_width      = gfxfw_.swapchain_->GetWidth();
    ngl::u32 screen_height     = gfxfw_.swapchain_->GetHeight();

    // Raytracing Scene更新.
    if (rt_scene_.IsValid())
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
        // ここではグラフィックスフレームワークが持つフレーム先頭コマンドリストにコマンド登録している.
        // Passを用意して通常のRtgパスとして実行するのが正規.
        rt_scene_.UpdateOnRender(&gfxfw_.device_, gfxfw_.p_system_frame_begin_command_list_, render_param_->frame_scene);
    }

    // SubViewの描画テスト.
    //	SubカメラでRTG描画をし, 伝搬指定した出力バッファをそのまま同一フレームのMainView描画で伝搬リソースとして利用するテスト.
    ngl::test::RenderFrameOut subview_render_frame_out{};
    if (dbgw_enable_sub_view_path)
    {
        // Pathの設定.
        ngl::test::RenderFrameDesc render_frame_desc{};
        {
            render_frame_desc.p_device = &gfxfw_.device_;

            render_frame_desc.screen_w = screen_width;
            render_frame_desc.screen_h = screen_height;

            render_frame_desc.camera_pos   = render_param_->camera_pos + ngl::math::Vec3(0.0f, 5.0f, 0.0f);
            render_frame_desc.camera_pose  = render_param_->camera_pose * ngl::math::Mat33::RotAxisX(ngl::math::Deg2Rad(75.0f));
            render_frame_desc.camera_fov_y = render_param_->camera_fov_y;

            render_frame_desc.p_scene               = &render_param_->frame_scene;
            render_frame_desc.directional_light_dir = render_param_->dlight_dir;

            {
                render_frame_desc.debug_multithread_render_pass    = dbgw_multithread_render_pass;
                render_frame_desc.debug_multithread_cascade_shadow = dbgw_multithread_cascade_shadow;
            }
            // SubViewは最低限の設定.
        }

        out_rtg_command_list_set.push_back({});
        ngl::rtg::RtgSubmitCommandSet& rtg_result = out_rtg_command_list_set.back();
        // Pathの実行 (RenderTaskGraphの構築と実行).
        TestFrameRenderingPath(render_frame_desc, subview_render_frame_out, gfxfw_.rtg_manager_, &rtg_result);
    }

    static ngl::rtg::RtgResourceHandle h_prev_light{};  // 前回フレームハンドルのテスト.
    // MainViewの描画.
    {
        constexpr ngl::rhi::EResourceState swapchain_final_state = ngl::rhi::EResourceState::Present;  // Execute後のステート指定.

        // Pathの設定.
        ngl::test::RenderFrameDesc render_frame_desc{};
        {
            render_frame_desc.p_device = &gfxfw_.device_;

            render_frame_desc.screen_w = screen_width;
            render_frame_desc.screen_h = screen_height;

            // MainViewはSwapchain書き込みPassを動かすため情報設定.
            render_frame_desc.ref_swapchain        = gfxfw_.swapchain_;
            render_frame_desc.ref_swapchain_rtv    = gfxfw_.swapchain_rtvs_[swapchain_index];
            render_frame_desc.swapchain_state_prev = swapchain_resource_state_[swapchain_index];
            render_frame_desc.swapchain_state_next = swapchain_final_state;

            render_frame_desc.camera_pos   = render_param_->camera_pos;
            render_frame_desc.camera_pose  = render_param_->camera_pose;
            render_frame_desc.camera_fov_y = render_param_->camera_fov_y;

            render_frame_desc.p_scene               = &render_param_->frame_scene;
            render_frame_desc.directional_light_dir = render_param_->dlight_dir;

            if (dbgw_enable_raytrace_pass && rt_scene_.IsValid())
            {
                // デバッグメニューからRaytracePassの有無切り替え.
                render_frame_desc.p_rt_scene = &rt_scene_;
            }

            if(ssvg_.IsValid())
            {
                render_frame_desc.p_ssvg = &ssvg_;
                render_frame_desc.is_enable_gi_lighting = (true) && dbgw_enable_gi_lighting;
            }

            render_frame_desc.ref_test_tex_srv      = res_texture_->ref_view_;
            render_frame_desc.h_prev_lit            = h_prev_light;  // MainViewはヒストリ有効.
            render_frame_desc.h_other_graph_out_tex = subview_render_frame_out.h_propagate_lit;

            {
                render_frame_desc.debug_multithread_render_pass    = dbgw_multithread_render_pass;
                render_frame_desc.debug_multithread_cascade_shadow = dbgw_multithread_cascade_shadow;

                render_frame_desc.debugview_halfdot_gray              = dbgw_view_half_dot_gray;
                render_frame_desc.debugview_enable_feedback_blur_test = dbgw_enable_feedback_blur_test;
                render_frame_desc.debugview_subview_result            = dbgw_enable_sub_view_path;
                render_frame_desc.debugview_raytrace_result           = dbgw_enable_raytrace_pass;

                render_frame_desc.debugview_gbuffer = dbgw_view_gbuffer;
                render_frame_desc.debugview_dshadow = dbgw_view_dshadow;
                render_frame_desc.debugview_ssvg_voxel = (0 <= ngl::render::app::SsVg::dbg_view_mode_);
                render_frame_desc.debugview_ssvg_voxel_rate = dbgw_view_ssvg_voxel_rate;
                render_frame_desc.debugview_ssvg_sky_visibility = dbgw_view_ssvg_sky_visibility;
            }
        }

        swapchain_resource_state_[swapchain_index] = swapchain_final_state;  // State変更.

        out_rtg_command_list_set.push_back({});
        ngl::rtg::RtgSubmitCommandSet& rtg_result = out_rtg_command_list_set.back();
        // Pathの実行 (RenderTaskGraphの構築と実行).
        ngl::test::RenderFrameOut render_frame_out{};
        TestFrameRenderingPath(render_frame_desc, render_frame_out, gfxfw_.rtg_manager_, &rtg_result);

        h_prev_light = render_frame_out.h_propagate_lit;  // Rtgリソースの一部を次フレームに伝搬する.

        // 統計情報取り込み.
        {
            dbgw_stat_primary_rtg_construct = render_frame_out.stat_rtg_construct_sec;
            dbgw_stat_primary_rtg_compile   = render_frame_out.stat_rtg_compile_sec;
            dbgw_stat_primary_rtg_execute   = render_frame_out.stat_rtg_execute_sec;
        }
    }
}

void PlayerController::UpdateFrame(ngl::platform::CoreWindow& window, float delta_sec, const ngl::math::Mat33& prev_camera_pose, const ngl::math::Vec3& prev_camera_pos)
{
    float camera_translate_speed = dbgw_camera_speed;

    const auto mouse_pos       = window.Dep().GetMousePosition();
    const auto mouse_pos_delta = window.Dep().GetMousePositionDelta();
    const bool mouse_l         = window.Dep().GetMouseLeft();
    const bool mouse_r         = window.Dep().GetMouseRight();
    const bool mouse_m         = window.Dep().GetMouseMiddle();

    ngl::math::Mat33 camera_pose = prev_camera_pose;
    ngl::math::Vec3 camera_pos   = prev_camera_pos;

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
            if (true)
            {
                // 適当に回転量へ.
                const auto rot_rad = ngl::math::k_pi_f * mouse_diff * 0.001f;
                auto rot_yaw       = ngl::math::Mat33::RotAxisY(rot_rad.x);
                auto rot_pitch     = ngl::math::Mat33::RotAxisX(rot_rad.y);
                // 回転.
                camera_pose = camera_pose * rot_yaw * rot_pitch;

                // sideベクトルをワールドXZ麺に制限.
                if (0.9999 > std::fabsf(camera_pose.GetColumn2().y))
                {
                    // 視線がY-Axisと不一致なら視線ベクトルとY-Axisから補正.
                    const float sign_y            = (0.0f < camera_pose.GetColumn1().y) ? 1.0f : -1.0f;
                    auto lx                       = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitY() * sign_y, camera_pose.GetColumn2());
                    auto ly                       = ngl::math::Vec3::Cross(camera_pose.GetColumn2(), lx);
                    const auto cam_pose_transpose = ngl::math::Mat33(ngl::math::Vec3::Normalize(lx), ngl::math::Vec3::Normalize(ly), camera_pose.GetColumn2());
                    camera_pose                   = ngl::math::Mat33::Transpose(cam_pose_transpose);
                }
                else
                {
                    // 視線がY-Axisと一致か近いならサイドベクトルのY成分潰して補正.
                    auto lx                       = camera_pose.GetColumn1();
                    lx                            = ngl::math::Vec3({lx.x, 0.0f, lx.z});
                    auto ly                       = ngl::math::Vec3::Cross(camera_pose.GetColumn2(), lx);
                    const auto cam_pose_transpose = ngl::math::Mat33(ngl::math::Vec3::Normalize(lx), ngl::math::Vec3::Normalize(ly), camera_pose.GetColumn2());
                    camera_pose                   = ngl::math::Mat33::Transpose(cam_pose_transpose);
                }
            }

            // 移動.
            {
                const auto vk_a = 65;  // VK_A.
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
    camera_pos_  = camera_pos;
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

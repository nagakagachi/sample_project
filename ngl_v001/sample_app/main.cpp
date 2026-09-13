// main.cpp: Sample application entry and scene/render setup.
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
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
#include "gfx/raytrace/raytrace_scene.h"
#include "render/scene/scene_mesh.h"
#include "render/scene/scene_skybox.h"

// マテリアルシェーダ関連.
#include "gfx/material/material_shader_generator.h"
#include "gfx/material/material_shader_manager.h"

// Render Path
#include "render/app/instant_rdv/instant_rdv.h"
#include "render/app/sw_tess/sw_tessellation_mesh.h"
#include "render/test_render_path.h"

// imguiのシステム処理Wrapper.
#include "imgui/imgui_interface.h"

// レイトレデモ機能の有効化
#define NGL_TEST_RAYTRACING_ENABLE 0
// SwTessellationデモ機能の有効化
#define NGL_TEST_SWTESSELLATION_ENABLE 0

// ImGui.
static bool dbgw_test_window_enable = true;

// デバッグ描画.
static bool dbgw_enable_feedback_blur_test = true;
static bool dbgw_enable_sub_view_path      = false;
static bool dbgw_enable_raytrace_pass      = false;
static bool dbgw_view_half_dot_gray        = false;
static bool dbgw_view_gbuffer              = false;
static bool dbgw_view_dshadow              = false;
static int dbgw_view_general_debug_buffer  = -1;
static int dbgw_view_general_debug_channel = 0;
static float dbgw_view_general_debug_rate  = 0.5f;

// InstantRdv. Instant Raster Derived Voxel scene.
static bool dbgw_view_instant_rdv_sky_visibility               = false;
static bool dbgw_enable_instant_rdv_sky_visibility_lighting    = true;
static bool dbgw_enable_instant_rdv_radiance_lighting          = true;
static int  dbgw_gi_sample_mode                         = ngl::test::EGiSampleMode_Fsp;
static float dbgw_gi_probe_sample_offset_view           = 0.2f;
static float dbgw_gi_probe_sample_offset_surface_normal = 0.2f;
static float dbgw_gi_probe_sample_offset_bent_normal    = 0.0f;
static bool dbgw_enable_instant_rdv_all_injection_pass = true;
static bool dbgw_enable_instant_rdv_all_removal_pass = true;
static bool dbgw_enable_instant_rdv_main_view_injection_pass = true;
static bool dbgw_enable_instant_rdv_main_view_removal_pass = true;
static bool dbgw_enable_instant_rdv_shadow_view_injection_pass = true;
static bool dbgw_enable_instant_rdv_shadow_view_removal_pass = true;
static bool dbgw_material_debug_emissive_enable = false;
static float dbgw_material_debug_emissive_intensity = 1.0f;
static ngl::math::Vec3 dbgw_material_debug_emissive_mask_albedo_color = ngl::math::Vec3(1.0f, 0.0f, 0.0f);
static float dbgw_material_debug_emissive_mask_tolerance = 0.4f;

static bool dbgw_enable_gtao_demo = false;

// Camera.
static float dbgw_camera_speed = 15.0f;  // 10.0f;

enum class EMeasurementViewLocation : int
{
    Startup = 0,
    GBufferStress = 1,
    Count
};

static int dbgw_measurement_view_location = static_cast<int>(EMeasurementViewLocation::Startup);
static int dbgw_applied_measurement_view_location = static_cast<int>(EMeasurementViewLocation::Startup);

struct MeasurementViewLocation
{
    const char* label;
    const char* command_name;
    ngl::math::Vec3 position;
    ngl::math::Mat33 pose;
};

static const std::array<MeasurementViewLocation, static_cast<size_t>(EMeasurementViewLocation::Count)>& GetMeasurementViewLocations()
{
    // 起動時Viewは既存のsample_app初期カメラを固定し、GBuffer候補は既存コメントの原点近傍カメラを計測地点として再利用する。
    static const std::array<MeasurementViewLocation, static_cast<size_t>(EMeasurementViewLocation::Count)> locations = {{
        {"Startup", "startup", {16.0f, 5.5f, -20.0f}, ngl::math::Mat33::RotAxisY(ngl::math::Deg2Rad(-40.0f))},
        {"GBuffer stress candidate", "gbuffer", {0.0f, 2.0f, -5.0f}, ngl::math::Mat33::Identity()}
    }};
    return locations;
}

static int ParseMeasurementViewLocation(const std::string& command_name)
{
    const auto& locations = GetMeasurementViewLocations();
    for (size_t index = 0; index < locations.size(); ++index)
    {
        if (command_name == locations[index].command_name)
        {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(EMeasurementViewLocation::Startup);
}

static void ApplyMeasurementViewLocation(
    int location_index,
    ngl::math::Vec3& camera_pos,
    ngl::math::Mat33& camera_pose)
{
    const auto& locations = GetMeasurementViewLocations();
    const int clamped_index = std::clamp(location_index, 0, static_cast<int>(locations.size()) - 1);
    camera_pos = locations[clamped_index].position;
    camera_pose = locations[clamped_index].pose;
}

// DLight.
static float dbgw_dlit_angle_v   = 0.4f;
static float dbgw_dlit_angle_h   = 4.1f;
static float dbgw_dlit_intensity = ngl::math::k_pi_f * 1.5f;

// Sky and IBL.
static int dbgw_sky_debug_mode       = {};
static float dbgw_sky_debug_mip_bias = 0.0f;
static float dbgw_skylight_intensity = 2.0f;

// 並列化.
static bool dbgw_render_thread                    = true;
static bool dbgw_multithread_render_pass          = true;
static bool dbgw_multithread_cascade_shadow       = true;
static float dbgw_perf_main_thread_sleep_millisec = 0.0f;
// Stat.
static float dbgw_stat_primary_rtg_construct = {};
static float dbgw_stat_primary_rtg_compile   = {};
static float dbgw_stat_primary_rtg_execute   = {};
#if NGL_ENABLE_GPU_SCOPE_PROFILER
static bool dbgw_show_gpu_profiler_window    = false;
static bool dbgw_gpu_profiler_latest_only    = true;
static bool dbgw_gpu_profiler_show_all_history = false;
static bool dbgw_gpu_profiler_hierarchy_view = true;
static bool dbgw_gpu_profiler_pause_updates = false;
#endif

// ベンチマークCLI実行設定.
struct BenchmarkCliOptions
{
    bool enabled = false;
    ngl::u32 warmup_frames = 60;
    double min_ready_seconds = 5.0;
    double start_timeout_seconds = 120.0;
    ngl::u32 measure_frames = 600;
    ngl::u32 ready_delta_stable_frames = 30;
    std::string output_dir = "artifacts\\perf\\latest";
    std::string tag = {};
    std::string view_location = "startup";
    int main_view_reduced_surface = -1;
};
static BenchmarkCliOptions g_benchmark_cli = {};

#if __has_include("sample_app_local_config.h")
#include "sample_app_local_config.h"
#endif

#if !defined(NGL_SAMPLE_APP_DEFAULT_SCENE_MODEL)
#define NGL_SAMPLE_APP_DEFAULT_SCENE_MODEL "../ngl/data/model/sponza_gltf/glTF/Sponza.gltf"
#endif
#if !defined(NGL_SAMPLE_APP_DEFAULT_SCENE_SCALE)
#define NGL_SAMPLE_APP_DEFAULT_SCENE_SCALE 1.0f
#endif

// 公開時はリポジトリに含まれるSponzaを使用し、ローカル設定またはCLIで上書きする。
struct SceneCliOptions
{
    std::string model_path = NGL_SAMPLE_APP_DEFAULT_SCENE_MODEL;
    float model_scale = NGL_SAMPLE_APP_DEFAULT_SCENE_SCALE;
};
static SceneCliOptions g_scene_cli = {};

// SwTessellation.
static float sw_tess_important_point_offset_in_view  = 7.0;
static int sw_tess_fixed_subdivision_level           = -1;     // -1で無効、0以上で固定分割レベルを指定
static bool sw_tess_update_tessellation              = true;   // trueでテッセレーション更新を有効化
static bool sw_tess_update_tessellation_frame_toggle = false;  // trueで1F毎にテッセレーション更新フラグをOFFにするデバッグ用.
static float sw_tess_split_threshold                 = 0.5f;   // テッセレーション分割閾値
static int sw_tess_debug_bisector_id                 = -1;     // デバッグ対象BisectorID（-1で無効）
static int sw_tess_debug_bisector_depth              = -1;     // デバッグ対象BisectorDepth（-1で無効）
static int sw_tess_debug_bisector_neighbor           = -1;

class PlayerController
{
private:
    bool prev_mouse_r_ = false;
    bool prev_shift_ = false;
    bool slow_camera_control_ = false;

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
    // ベンチマーク実行設定をCLI引数から反映.
    void ConfigureBenchmarkFromCli();
    // 最新フレームのGPUマーカー統計を1サンプル収集.
    bool CaptureBenchmarkFrameStats();
    // ベンチマーク結果をJSON/CSVへ保存.
    bool SaveBenchmarkReport() const;
    // AppのRenderThread処理.
    void RenderApp(ngl::fwk::RtgFrameRenderSubmitCommandBuffer& out_rtg_command_list_set);

private:
    struct RenderParam
    {
        ngl::math::Vec3 camera_pos   = {0.0f, 0.0f, 0.0f};
        ngl::math::Mat33 camera_pose = ngl::math::Mat33::Identity();
        float camera_fov_y           = ngl::math::Deg2Rad(60.0f);  // not half fov.
        ngl::math::Vec3 prev_camera_pos   = {0.0f, 0.0f, 0.0f};
        ngl::math::Mat33 prev_camera_pose = ngl::math::Mat33::Identity();

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

    // ngl::math::Vec3 camera_pos_   = {0.368f, 1.237f, 0.453f};//{0.0f, 2.0f, -5.0f};
    ngl::math::Vec3 camera_pos_   = {16.0f, 5.5f, -20.0f}; //{1.871f, 1.347f, 1.399f};
    ngl::math::Mat33 camera_pose_ = ngl::math::Mat33::RotAxisY(ngl::math::Deg2Rad(-40.0f));  // ngl::math::Mat33::Identity();
    float camera_fov_y            = ngl::math::Deg2Rad(60.0f);                               // not half fov.
    ngl::math::Vec3 prev_camera_pos_ = {0.0f, 0.0f, 0.0f};
    ngl::math::Mat33 prev_camera_pose_ = ngl::math::Mat33::Identity();
    PlayerController player_controller{};

    // GfxSceneMesh版
    std::vector<std::shared_ptr<ngl::gfx::scene::SceneMesh>> mesh_entity_array_;
    std::vector<ngl::gfx::scene::SceneMesh*> test_move_mesh_entity_array_;

    // SwTessellationMesh管理用
    std::vector<ngl::render::app::SwTessellationMesh*> sw_tessellation_mesh_array_;

    // RaytraceScene.
    ngl::gfx::RtSceneManager rt_scene_;

    ngl::render::app::InstantRasterDerivedVoxelScene instant_rdv_;

    ngl::fwk::GfxScene gfx_scene_{};
    ngl::gfx::scene::SceneSkyBox skybox_{};

    ngl::rhi::RefTextureDep tex_rw_;
    ngl::rhi::RefSrvDep tex_rw_srv_;
    ngl::rhi::RefUavDep tex_rw_uav_;

    // Loaded Texture.
    ngl::res::ResourceHandle<ngl::gfx::ResTexture> res_texture_{};

    struct BenchmarkMarkerSamples
    {
        std::vector<double> last_ms_history = {};
        ngl::fwk::GraphicsFramework::GpuScopeStatLatest latest = {};
    };
    bool benchmark_enabled_ = false;
    ngl::u32 benchmark_warmup_frames_ = 0;
    double benchmark_min_ready_seconds_ = 5.0;
    double benchmark_start_timeout_seconds_ = 120.0;
    ngl::u32 benchmark_measure_frames_ = 0;
    ngl::u32 benchmark_ready_delta_stable_frames_ = 30;
    ngl::u32 benchmark_delta_stable_counter_ = 0;
    bool benchmark_start_timeout_logged_ = false;
    bool benchmark_measure_started_logged_ = false;
    ngl::u32 benchmark_measure_start_frame_ = 0;
    double benchmark_measure_start_app_sec_ = 0.0;
    std::string benchmark_measure_start_reason_ = {};
    ngl::u32 benchmark_app_frame_counter_ = 0;
    ngl::u32 benchmark_captured_frame_counter_ = 0;
    std::string benchmark_output_dir_ = {};
    std::string benchmark_tag_ = {};
    std::vector<std::string> benchmark_marker_order_ = {};
    std::unordered_map<std::string, BenchmarkMarkerSamples> benchmark_marker_samples_ = {};
};

// -----------------------------------------------
// テストコードの呼び出しはここに書く.
static void TestEntry()
{
    // テストコード呼び出し.
    ngl::thread::TestLockFreeStackIntrusive();
    ngl::thread::TestFixedSizeLockFreeStack();
    ngl::thread::TestStaticSizeLockFreeStack();

    ngl::math::math_test();

    ngl::render::app::ConcurrentBinaryTreeU32::Test();
}

static bool TryParseU32Arg(const std::string& src, ngl::u32& out_value)
{
    try
    {
        const auto parsed = std::stoul(src);
        if (parsed > std::numeric_limits<ngl::u32>::max())
        {
            return false;
        }
        out_value = static_cast<ngl::u32>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static std::string EscapeJsonString(const std::string& src)
{
    std::string out = {};
    out.reserve(src.size() + 8);
    for (const char c : src)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '\"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

static double CalcPercentileFromSorted(const std::vector<double>& sorted, double percentile01)
{
    if (sorted.empty())
    {
        return 0.0;
    }
    const double clamped = std::clamp(percentile01, 0.0, 1.0);
    const size_t index = static_cast<size_t>(std::floor((sorted.size() - 1) * clamped));
    return sorted[index];
}

static std::string BuildUtcTimestampIso8601()
{
    const auto now = std::chrono::system_clock::now();
    const auto now_tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &now_tt);
#else
    gmtime_r(&now_tt, &utc_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static void ParseCommandLineArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--benchmark" || arg == "benchmark" || arg == "bench" || arg == "perf-run" || arg == "ベンチマーク")
        {
            g_benchmark_cli.enabled = true;
        }
        else if (arg == "--benchmark-warmup" && (i + 1) < argc)
        {
            ngl::u32 parsed = 0;
            if (TryParseU32Arg(argv[++i], parsed))
            {
                g_benchmark_cli.warmup_frames = parsed;
            }
        }
        else if (arg == "--benchmark-measure" && (i + 1) < argc)
        {
            ngl::u32 parsed = 0;
            if (TryParseU32Arg(argv[++i], parsed))
            {
                g_benchmark_cli.measure_frames = std::max<ngl::u32>(1u, parsed);
            }
        }
        else if (arg == "--benchmark-start-timeout-sec" && (i + 1) < argc)
        {
            try
            {
                g_benchmark_cli.start_timeout_seconds = std::max(1.0, std::stod(argv[++i]));
                g_benchmark_cli.enabled = true;
            }
            catch (...)
            {
            }
        }
        else if (arg == "--benchmark-min-ready-sec" && (i + 1) < argc)
        {
            try
            {
                g_benchmark_cli.min_ready_seconds = std::max(0.0, std::stod(argv[++i]));
                g_benchmark_cli.enabled = true;
            }
            catch (...)
            {
            }
        }
        else if (arg == "--benchmark-output" && (i + 1) < argc)
        {
            g_benchmark_cli.output_dir = argv[++i];
            g_benchmark_cli.enabled = true;
        }
        else if (arg == "--benchmark-ready-delta-frames" && (i + 1) < argc)
        {
            ngl::u32 parsed = 0;
            if (TryParseU32Arg(argv[++i], parsed))
            {
                g_benchmark_cli.ready_delta_stable_frames = std::max<ngl::u32>(1u, parsed);
                g_benchmark_cli.enabled = true;
            }
        }
        else if (arg == "--benchmark-tag" && (i + 1) < argc)
        {
            g_benchmark_cli.tag = argv[++i];
            g_benchmark_cli.enabled = true;
        }
        else if (arg == "--benchmark-view" && (i + 1) < argc)
        {
            g_benchmark_cli.view_location = argv[++i];
            g_benchmark_cli.enabled = true;
        }
        else if (arg == "--main-view-reduced-surface" && (i + 1) < argc)
        {
            const std::string value = argv[++i] ? argv[i] : "";
            if(value == "on")
            {
                g_benchmark_cli.main_view_reduced_surface = 1;
            }
            else if(value == "off")
            {
                g_benchmark_cli.main_view_reduced_surface = 0;
            }
        }
        else if (arg == "--scene-model" && (i + 1) < argc)
        {
            g_scene_cli.model_path = argv[++i] ? argv[i] : "";
        }
        else if (arg == "--scene-scale" && (i + 1) < argc)
        {
            try
            {
                const float parsed_scale = std::stof(argv[++i]);
                if(std::isfinite(parsed_scale) && parsed_scale > 0.0f)
                {
                    g_scene_cli.model_scale = parsed_scale;
                }
            }
            catch (...)
            {
            }
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout
                << "Usage: sample_app [--benchmark] [--benchmark-warmup N] [--benchmark-measure N] "
                << "[--benchmark-start-timeout-sec S] [--benchmark-min-ready-sec S] "
                << "[--benchmark-output PATH] [--benchmark-tag LABEL] [--benchmark-view startup|gbuffer]" << std::endl
                << "       [--benchmark-ready-delta-frames N] "
                << "[--main-view-reduced-surface on|off]" << std::endl
                << "       [--scene-model PATH] [--scene-scale SCALE]" << std::endl
                << "  benchmark aliases: benchmark / bench / perf-run / ベンチマーク" << std::endl;
        }
    }
}

AppGame::AppGame()
{
    prev_camera_pos_ = camera_pos_;
    prev_camera_pose_ = camera_pose_;
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
        instant_rdv_.Finalize();

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
    if (!window_.Initialize(_T("ToyRenderer"), scree_w, scree_h, g_benchmark_cli.enabled))
    {
        return false;
    }
    // グラフィックスフレームワーク初期化.
    ngl::fwk::GraphicsFramework::Desc gfxfw_desc{};
    gfxfw_desc.require_enhanced_barrier = true;
    if (!gfxfw_.Initialize(&window_, gfxfw_desc))
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

        //const char* mesh_file_box = "K:\\GitHub\\sample_projct_lib\\ngl_v001\\ngl\\external\\assimp\\test\\models\\FBX\\box.fbx";
        const char* mesh_file_box = "../ngl/data/model/assimp/FBX/box.fbx";

        // 既定ではコミット済みのSponzaを使用する。ローカル設定またはCLIで上書きできる。
        const char* mesh_target_scene = g_scene_cli.model_path.c_str();
        const float target_scene_base_scale = g_scene_cli.model_scale;
        {
            std::error_code ec;
            const bool is_mesh_target_scene_exists = std::filesystem::exists(mesh_target_scene, ec);
            if (!is_mesh_target_scene_exists)
            {
                std::cout << "[ERROR] Scene model file was not found: " << mesh_target_scene << std::endl;
                std::cout << "Use --scene-model PATH to select a local model." << std::endl;
                if(ec)
                {
                    std::cout << "filesystem error: " << ec.message() << std::endl;
                }
                return false;
            }
        }

        std::shared_ptr<ngl::gfx::MeshData> procedural_mesh_data = std::make_shared<ngl::gfx::MeshData>();
        {
            const float mesh_scale      = 10.0f;
            ngl::math::Vec3 quad_pos[4] = {
                ngl::math::Vec3(-1.0f, 0.0f, -1.0f) * mesh_scale,
                ngl::math::Vec3(-1.0f, 0.0f, 1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, 1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, -1.0f) * mesh_scale};
            ngl::math::Vec3 quad_normal[4] = {
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f)};
            ngl::math::Vec3 quad_tangent[4] = {
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f)};
            ngl::math::Vec3 quad_binormal[4] = {
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f)};
            ngl::math::Vec2 quad_texcoord[4] = {
                ngl::math::Vec2(0.0f, 0.0f),
                ngl::math::Vec2(1.0f, 0.0f),
                ngl::math::Vec2(0.0f, 1.0f),
                ngl::math::Vec2(1.0f, 1.0f)};
            ngl::gfx::VertexColor quad_color[4] = {
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255}};

            ngl::u32 index_data[6] = {
                0, 1, 2,
                0, 2, 3};

            ngl::gfx::MeshShapeInitializeSourceData init_source_data{};
            init_source_data.num_vertex_    = 4;
            init_source_data.num_primitive_ = 2;
            init_source_data.index_         = index_data;
            init_source_data.position_      = quad_pos;
            init_source_data.normal_        = quad_normal;
            init_source_data.tangent_       = quad_tangent;
            init_source_data.binormal_      = quad_binormal;
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
                // tr.SetColumn3(ngl::math::Vec4(30.0f, 12.0f, 0.0f, 1.0f));
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
            for (int i = 0; i < 10; ++i)
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
#else
            // テスト用に固定位置で一つだけ生成
            {
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);
                ngl::gfx::ResMeshData::LoadDesc loaddesc{};
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc));

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                tr.SetDiagonal(ngl::math::Vec4(spider_base_scale * 4.0f));
                tr.SetColumn3(ngl::math::Vec4(-10.0f, 10.0f, -2.0f, 1.0f));

                mc->SetTransform(ngl::math::Mat34(tr));

                // 移動テスト用.
                test_move_mesh_entity_array_.push_back(mc.get());
            }
#endif

            // 単純形状のプログラム生成メッシュを登録するテスト.
            if (0)
            {
                auto mc = std::make_shared<ngl::gfx::scene::SceneMesh>();
                mesh_entity_array_.push_back(mc);

                ngl::gfx::ResMeshData::LoadDesc loaddesc{};

                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc), procedural_mesh_data);
                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                tr.SetColumn3(ngl::math::Vec4(-20.0f, 20.0f, 0.0f, 0.0f));
                mc->SetTransform(ngl::math::Mat34(tr));
            }

            // SwTessellationのテスト.
#if NGL_TEST_SWTESSELLATION_ENABLE
            {
                auto mc = std::make_shared<ngl::render::app::SwTessellationMesh>();
                mesh_entity_array_.push_back(mc);

                // SwTessellationMesh管理用vectorにも追加
                sw_tessellation_mesh_array_.push_back(mc.get());

                ngl::gfx::ResMeshData::LoadDesc loaddesc{};

                ngl::math::Mat44 tr = ngl::math::Mat44::Identity();
                constexpr int tessellation_level = 9;  // 0で無効、1以上で有効.
#if 1
                // 蜘蛛
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc), {}, tessellation_level);
                tr.SetDiagonal(ngl::math::Vec4(spider_base_scale * 9.0f));
                tr.SetColumn3(ngl::math::Vec4(-8.0f, 12.0f, 2.0f, 0.0f));
#else
                mc->Initialize(&device, &gfx_scene_, ResourceMan.LoadResource<ngl::gfx::ResMeshData>(&device, mesh_file_spider, &loaddesc), procedural_mesh_data, tessellation_level);
                tr.SetColumn3(ngl::math::Vec4(0.0f, 10.0f, 0.0f, 0.0f));
#endif
                mc->SetTransform(ngl::math::Mat34(tr));
            }
#endif
        }
    }

    // Raytrace.
    //  初期化しなければ描画パスでも処理されなくなる.
#if NGL_TEST_RAYTRACING_ENABLE
    //	TLAS構築時にShaderTableの最大Hitgroup数が必要な設計であるため初期化時に最大数指定する. PrimayとShadowの2種であれば 2.
    constexpr int k_system_hitgroup_count_max = 3;
    if (!rt_scene_.Initialize(&device, k_system_hitgroup_count_max))
    {
        std::cout << "[ERROR] Initialize gfx::RtSceneManager" << std::endl;
    }
#endif

#if 1
    // InstantRdv.
    instant_rdv_.Initialize(&device, ngl::math::Vec3u(64), 3.0f, ngl::math::Vec3u(16), 2.0f, 5);
    ngl::render::app::InstantRasterDerivedVoxelScene::dbg_view_category_ = 0;
    ngl::render::app::InstantRasterDerivedVoxelScene::dbg_view_sub_mode_ = 0;
#endif

    // Texture Rexource読み込みのテスト.
    ngl::gfx::ResTexture::LoadDesc tex_load_desc{};
    // const char test_load_texture_file_name[] = "../ngl/data/model/sponza_gltf/glTF/6772804448157695701.jpg";
    const char test_load_texture_file_name[] = "../ngl/data/texture/sample_dds/test-dxt1.dds";
    // const char test_load_texture_file_name[] = "../ngl/data/texture/vgl/pisa/pisa.hdr";
    res_texture_ = ngl::res::ResourceManager::Instance().LoadResource<ngl::gfx::ResTexture>(&device, test_load_texture_file_name, &tex_load_desc);

    ngl::time::Timer::Instance().StartTimer("app_frame_sec");
    ConfigureBenchmarkFromCli();
    return true;
}

void AppGame::ConfigureBenchmarkFromCli()
{
    benchmark_enabled_ = g_benchmark_cli.enabled;
    if (!benchmark_enabled_)
    {
        return;
    }

    benchmark_warmup_frames_ = g_benchmark_cli.warmup_frames;
    benchmark_min_ready_seconds_ = std::max(0.0, g_benchmark_cli.min_ready_seconds);
    benchmark_start_timeout_seconds_ = g_benchmark_cli.start_timeout_seconds;
    benchmark_measure_frames_ = std::max<ngl::u32>(1u, g_benchmark_cli.measure_frames);
    benchmark_ready_delta_stable_frames_ = std::max<ngl::u32>(1u, g_benchmark_cli.ready_delta_stable_frames);
    benchmark_output_dir_ = g_benchmark_cli.output_dir;
    benchmark_tag_ = g_benchmark_cli.tag;
    dbgw_measurement_view_location = ParseMeasurementViewLocation(g_benchmark_cli.view_location);
    ApplyMeasurementViewLocation(dbgw_measurement_view_location, camera_pos_, camera_pose_);
    prev_camera_pos_ = camera_pos_;
    prev_camera_pose_ = camera_pose_;
    dbgw_applied_measurement_view_location = dbgw_measurement_view_location;
    if(g_benchmark_cli.main_view_reduced_surface >= 0)
    {
        ngl::render::app::InstantRasterDerivedVoxelScene::
            dbg_main_view_reduced_surface_enable_ =
                g_benchmark_cli.main_view_reduced_surface != 0;
    }

    // ベンチマーク実行時はUI操作依存を外す.
    dbgw_test_window_enable = false;
#if NGL_ENABLE_GPU_SCOPE_PROFILER
    dbgw_show_gpu_profiler_window = false;
    dbgw_gpu_profiler_pause_updates = false;
#endif

    std::cout << "[Benchmark] enabled. warmup=" << benchmark_warmup_frames_
              << " min_ready_sec=" << benchmark_min_ready_seconds_
              << " start_timeout_sec=" << benchmark_start_timeout_seconds_
              << " measure=" << benchmark_measure_frames_
              << " delta_stable_frames=" << benchmark_ready_delta_stable_frames_
              << " view=" << GetMeasurementViewLocations()[dbgw_measurement_view_location].command_name
              << " output=" << benchmark_output_dir_ << std::endl;
}

bool AppGame::CaptureBenchmarkFrameStats()
{
    std::vector<ngl::fwk::GraphicsFramework::GpuScopeStatEntry> all_entries = {};
    ngl::u64 latest_frame_id = 0;
    gfxfw_.EnumerateLatestGpuScopeStats([&all_entries, &latest_frame_id](const ngl::fwk::GraphicsFramework::GpuScopeStatEntry& e)
    {
        if (!e.stat.valid || !e.label)
        {
            return;
        }
        latest_frame_id = std::max(latest_frame_id, e.stat.frame_id);
        all_entries.push_back(e);
    });

    if (all_entries.empty() || latest_frame_id == 0)
    {
        return false;
    }

    bool has_sample = false;
    for (const auto& e : all_entries)
    {
        if (e.stat.frame_id != latest_frame_id)
        {
            continue;
        }
        const std::string label = e.label;
        auto& marker = benchmark_marker_samples_[label];
        if (marker.last_ms_history.empty())
        {
            benchmark_marker_order_.push_back(label);
        }
        marker.last_ms_history.push_back(e.stat.last_ms);
        marker.latest = e.stat;
        has_sample = true;
    }
    return has_sample;
}

bool AppGame::SaveBenchmarkReport() const
{
    namespace fs = std::filesystem;
#if defined(_DEBUG)
    constexpr const char* k_build_config = "Debug";
#else
    constexpr const char* k_build_config = "Release";
#endif

    fs::path out_dir = benchmark_output_dir_;
    if (out_dir.empty())
    {
        out_dir = "artifacts\\perf\\latest";
    }
    std::error_code ec{};
    fs::create_directories(out_dir, ec);
    if (ec)
    {
        std::cout << "[Benchmark] Failed to create output dir: " << out_dir.string() << std::endl;
        return false;
    }

    struct MarkerSummary
    {
        std::string label = {};
        ngl::u32 sample_count = 0;
        double last_ms = 0.0;
        double avg_ms = 0.0;
        double p50_ms = 0.0;
        double p95_ms = 0.0;
        double max_ms = 0.0;
        ngl::u64 frame_id = 0;
    };

    std::vector<MarkerSummary> summaries = {};
    summaries.reserve(benchmark_marker_samples_.size());
    for (const auto& label : benchmark_marker_order_)
    {
        const auto it = benchmark_marker_samples_.find(label);
        if (it == benchmark_marker_samples_.end())
        {
            continue;
        }
        const auto& hist = it->second.last_ms_history;
        if (hist.empty())
        {
            continue;
        }

        MarkerSummary s = {};
        s.label = label;
        s.sample_count = static_cast<ngl::u32>(hist.size());
        s.last_ms = hist.back();
        s.avg_ms = std::accumulate(hist.begin(), hist.end(), 0.0) / static_cast<double>(hist.size());
        s.max_ms = *std::max_element(hist.begin(), hist.end());

        auto sorted = hist;
        std::sort(sorted.begin(), sorted.end());
        s.p50_ms = CalcPercentileFromSorted(sorted, 0.50);
        s.p95_ms = CalcPercentileFromSorted(sorted, 0.95);
        s.frame_id = it->second.latest.frame_id;
        summaries.push_back(s);
    }

    const fs::path csv_path = out_dir / "benchmark_gpu_markers.csv";
    const fs::path json_path = out_dir / "benchmark_gpu_markers.json";

    {
        std::ofstream ofs(csv_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open())
        {
            std::cout << "[Benchmark] Failed to open CSV: " << csv_path.string() << std::endl;
            return false;
        }
        ofs << "scope,sample_count,last_ms,avg_ms,p50_ms,p95_ms,max_ms,frame_id\n";
        for (const auto& s : summaries)
        {
            ofs << "\"" << s.label << "\""
                << "," << s.sample_count
                << "," << s.last_ms
                << "," << s.avg_ms
                << "," << s.p50_ms
                << "," << s.p95_ms
                << "," << s.max_ms
                << "," << s.frame_id
                << "\n";
        }
    }

    {
        std::ofstream ofs(json_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open())
        {
            std::cout << "[Benchmark] Failed to open JSON: " << json_path.string() << std::endl;
            return false;
        }
        ofs << "{\n";
        ofs << "  \"format_version\": 1,\n";
        ofs << "  \"mode\": \"benchmark\",\n";
        ofs << "  \"timestamp_utc\": \"" << EscapeJsonString(BuildUtcTimestampIso8601()) << "\",\n";
        ofs << "  \"build_config\": \"" << k_build_config << "\",\n";
        ofs << "  \"tag\": \"" << EscapeJsonString(benchmark_tag_) << "\",\n";
        ofs << "  \"warmup_frames\": " << benchmark_warmup_frames_ << ",\n";
        ofs << "  \"start_timeout_seconds\": " << benchmark_start_timeout_seconds_ << ",\n";
        ofs << "  \"ready_delta_stable_frames\": " << benchmark_ready_delta_stable_frames_ << ",\n";
        ofs << "  \"measure_start_reason\": \"" << EscapeJsonString(benchmark_measure_start_reason_) << "\",\n";
        ofs << "  \"measure_start_frame\": " << benchmark_measure_start_frame_ << ",\n";
        ofs << "  \"measure_start_app_seconds\": " << benchmark_measure_start_app_sec_ << ",\n";
        ofs << "  \"measure_frames_target\": " << benchmark_measure_frames_ << ",\n";
        ofs << "  \"measure_frames_captured\": " << benchmark_captured_frame_counter_ << ",\n";
        ofs << "  \"app_frames_total\": " << benchmark_app_frame_counter_ << ",\n";
        ofs << "  \"main_view_reduced_surface\": "
            << (ngl::render::app::InstantRasterDerivedVoxelScene::
                    dbg_main_view_reduced_surface_enable_
                    ? "true"
                    : "false")
            << ",\n";
        ofs << "  \"fsp_visible_surface_cell_count\": "
            << ngl::render::app::InstantRasterDerivedVoxelScene::
                dbg_fsp_visible_surface_cell_count_
            << ",\n";
        ofs << "  \"fsp_active_probe_count\": "
            << ngl::render::app::InstantRasterDerivedVoxelScene::
                dbg_fsp_active_probe_count_
            << ",\n";
        ofs << "  \"markers\": [\n";
        for (size_t i = 0; i < summaries.size(); ++i)
        {
            const auto& s = summaries[i];
            ofs << "    {\n";
            ofs << "      \"scope\": \"" << EscapeJsonString(s.label) << "\",\n";
            ofs << "      \"sample_count\": " << s.sample_count << ",\n";
            ofs << "      \"last_ms\": " << s.last_ms << ",\n";
            ofs << "      \"avg_ms\": " << s.avg_ms << ",\n";
            ofs << "      \"p50_ms\": " << s.p50_ms << ",\n";
            ofs << "      \"p95_ms\": " << s.p95_ms << ",\n";
            ofs << "      \"max_ms\": " << s.max_ms << ",\n";
            ofs << "      \"frame_id\": " << s.frame_id << "\n";
            ofs << "    }" << ((i + 1) < summaries.size() ? "," : "") << "\n";
        }
        ofs << "  ]\n";
        ofs << "}\n";
    }

    std::cout << "[Benchmark] Saved: " << json_path.string() << std::endl;
    std::cout << "[Benchmark] Saved: " << csv_path.string() << std::endl;
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
    if (dbgw_measurement_view_location != dbgw_applied_measurement_view_location)
    {
        ApplyMeasurementViewLocation(dbgw_measurement_view_location, camera_pos_, camera_pose_);
        prev_camera_pose_ = camera_pose_;
        prev_camera_pos_ = camera_pos_;
        dbgw_applied_measurement_view_location = dbgw_measurement_view_location;
    }
    else
    {
        prev_camera_pose_ = camera_pose_;
        prev_camera_pos_ = camera_pos_;
    }

    if (!benchmark_enabled_)
    {
            player_controller.UpdateFrame(window_, delta_sec, camera_pose_, camera_pos_);
            camera_pose_ = player_controller.camera_pose_;
            camera_pos_  = player_controller.camera_pos_;
        }
    }

    {
        // 初期位置とサイズ.
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 1500, main_viewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 400.0f), ImVec2(FLT_MAX, FLT_MAX));

        ImGui::Begin("Debug Window", &dbgw_test_window_enable, ImGuiWindowFlags_None);
        const float debug_window_item_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 120.0f, 180.0f);
        ImGui::PushItemWidth(debug_window_item_width);

        ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");
        ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), "[Camera Control]");
        ImGui::TextColored(ImColor(1.0f, 0.9f, 0.9f), "  Right Mouse Button + WASD + SPACE + CTRL");
        ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");
        ImGui::TextColored(ImColor(1.0f, 0.9f, 0.9f), "     (Unreal Engine Like)");
        ImGui::TextColored(ImColor(1.0f, 0.2f, 0.2f), " ");

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("View Info"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            ImGui::Text("Camera Dir:			%.3f, %.3f, %.3f", camera_pose_.GetColumn2().x, camera_pose_.GetColumn2().y, camera_pose_.GetColumn2().z);
            ImGui::Text("Camera Pos:			%.3f, %.3f, %.3f", camera_pos_.x, camera_pos_.y, camera_pos_.z);

            const auto& measurement_views = GetMeasurementViewLocations();
            const char* measurement_view_labels[] = {
                measurement_views[0].label,
                measurement_views[1].label
            };
            if (ImGui::Combo("Measurement View", &dbgw_measurement_view_location, measurement_view_labels, static_cast<int>(measurement_views.size())))
            {
                dbgw_measurement_view_location = std::clamp(
                    dbgw_measurement_view_location,
                    0,
                    static_cast<int>(measurement_views.size()) - 1);
            }
            ImGui::TextDisabled("Select a fixed camera for repeatable GPU measurements.");

            ImGui::SliderFloat("Camera Speed", &dbgw_camera_speed, 0.5f, 100.0f);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Debug Perf"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
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
#if NGL_ENABLE_GPU_SCOPE_PROFILER
            // メインウィンドウから専用GPUプロファイラウィンドウを開閉.
            ImGui::Checkbox("Show GPU Profiler Window", &dbgw_show_gpu_profiler_window);
#endif
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Debug View"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            ImGui::Checkbox("View GBuffer", &dbgw_view_gbuffer);
            ImGui::Checkbox("View Directional Shadow Atlas", &dbgw_view_dshadow);
            ImGui::Checkbox("View Half Dot Gray", &dbgw_view_half_dot_gray);
            // SH から再評価した sky visibility デバッグ.
            ImGui::Checkbox("View GI Sky Visibility (SH)", &dbgw_view_instant_rdv_sky_visibility);

            ImGui::Separator();
            ImGui::Text("Debug Buffer Visualize");
            {
                ImGui::RadioButton("None", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::None);
                ImGui::RadioButton("GBuffer0", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::GBuffer0);
                ImGui::RadioButton("GBuffer1", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::GBuffer1);
                ImGui::RadioButton("GBuffer2", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::GBuffer2);
                ImGui::RadioButton("GBuffer3", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::GBuffer3);
                ImGui::RadioButton("HwDepth", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::HardwareDepth);
                ImGui::RadioButton("DirectionalShadowAtlas", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::DirectionalShadowAtlas);
                ImGui::RadioButton("GtaoDemo", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::GtaoDemo);
                ImGui::RadioButton("BentNormalTest", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::BentNormalTest);
                ImGui::RadioButton("InstantRdvDebugTexture", &dbgw_view_general_debug_buffer, ngl::test::EDebugBufferMode::InstantRdvDebugTexture);
            }
            ImGui::SliderInt("Channel", &dbgw_view_general_debug_channel, 0, 10);
            ImGui::SliderFloat("Slider Rate", &dbgw_view_general_debug_rate, 0.0f, 1.0f);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Lighting"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                ImGui::SliderFloat("Angle V", &dbgw_dlit_angle_v, 0.0f, ngl::math::k_pi_f * 2.0f);
                ImGui::SliderFloat("Angle H", &dbgw_dlit_angle_h, 0.0f, ngl::math::k_pi_f * 2.0f);
                ImGui::SliderFloat("Intensity", &dbgw_dlit_intensity, 0.0f, 50.0f);
            }
            if (ImGui::CollapsingHeader("IBL", ImGuiTreeNodeFlags_DefaultOpen))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                ImGui::SliderFloat("Sky Light Intensity", &dbgw_skylight_intensity, 0.0f, 15.0f);
            }
            if (ImGui::CollapsingHeader("GI##Lighting", ImGuiTreeNodeFlags_DefaultOpen))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                ImGui::Text("Sample Mode");
                ImGui::RadioButton("None##LightingGiSampleMode", &dbgw_gi_sample_mode, ngl::test::EGiSampleMode_None);
                ImGui::SameLine();
                ImGui::RadioButton("ASSP##LightingGiSampleMode", &dbgw_gi_sample_mode, ngl::test::EGiSampleMode_Assp);
                ImGui::SameLine();
                ImGui::RadioButton("FSP##LightingGiSampleMode", &dbgw_gi_sample_mode, ngl::test::EGiSampleMode_Fsp);
                ImGui::Checkbox("Enable SkyVisibility", &dbgw_enable_instant_rdv_sky_visibility_lighting);
                ImGui::Checkbox("Enable Irradiance", &dbgw_enable_instant_rdv_radiance_lighting);
                ImGui::SliderFloat("Sample Offset View", &dbgw_gi_probe_sample_offset_view, 0.0f, 10.0f);
                ImGui::SliderFloat("Sample Offset Surface Normal", &dbgw_gi_probe_sample_offset_surface_normal, 0.0f, 10.0f);
                ImGui::SliderFloat("Sample Offset Bent Normal", &dbgw_gi_probe_sample_offset_bent_normal, 0.0f, 10.0f);
            }
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("GI##InstantRdv"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            ngl::render::app::InstantRasterDerivedVoxelScene::DrawDebugMenu(
                &dbgw_enable_instant_rdv_all_injection_pass,
                &dbgw_enable_instant_rdv_all_removal_pass,
                &dbgw_enable_instant_rdv_main_view_injection_pass,
                &dbgw_enable_instant_rdv_main_view_removal_pass,
                &dbgw_enable_instant_rdv_shadow_view_injection_pass,
                &dbgw_enable_instant_rdv_shadow_view_removal_pass);

            if (ImGui::CollapsingHeader("GTAO Demo"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
                ImGui::Checkbox("enable", &dbgw_enable_gtao_demo);
            }
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Material Debug"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            ImGui::Checkbox("Debug Emissive ON/OFF", &dbgw_material_debug_emissive_enable);
            ImGui::SliderFloat("Debug Emissive Intensity", &dbgw_material_debug_emissive_intensity, 0.0f, 30.0f);
            ImGui::ColorEdit3("Debug Emissive Mask Albedo Color", &dbgw_material_debug_emissive_mask_albedo_color.x);
            ImGui::SliderFloat("Debug Emissive Mask Tolerance", &dbgw_material_debug_emissive_mask_tolerance, 0.0f, 1.7320508f, "%.3f");
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Sky"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            if (ImGui::CollapsingHeader("IBL"))
            {
                NGL_IMGUI_SCOPED_INDENT(10.0f);
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
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            ImGui::Checkbox("Enable Feedback Blur Test", &dbgw_enable_feedback_blur_test);
            ImGui::Checkbox("Enable Raytrace Pass", &dbgw_enable_raytrace_pass);
            ImGui::Checkbox("Enable SubView Render", &dbgw_enable_sub_view_path);
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("SwTessellation Mesh"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
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
                sw_tess_debug_bisector_id       = -1;
                sw_tess_debug_bisector_depth    = -1;
                sw_tess_debug_bisector_neighbor = -1;
            }
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("RHI"))
        {
            NGL_IMGUI_SCOPED_INDENT(10.0f);
            const auto free_dynamic_descriptor_count = gfxfw_.device_.GeDynamicDescriptorManager()->GetFreeDescriptorCount();
            const auto max_dynamic_descriptor_count  = gfxfw_.device_.GeDynamicDescriptorManager()->GetMaxDescriptorCount();
            // Dynamic Descriptorの残量.
            ImGui::Text("DynamicDescriptor Free Count : %d / %d (%.2f)",
                        free_dynamic_descriptor_count, max_dynamic_descriptor_count, 100.0f * (float)free_dynamic_descriptor_count / (float)max_dynamic_descriptor_count);
        }

        ImGui::PopItemWidth();
        ImGui::End();
    }

#if NGL_ENABLE_GPU_SCOPE_PROFILER
    if (dbgw_show_gpu_profiler_window)
    {
        // GPU計測専用ウィンドウ.
        ImGui::SetNextWindowSize(ImVec2(780.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("GPU Profiler", &dbgw_show_gpu_profiler_window, ImGuiWindowFlags_None))
        {
            struct UiGpuScopeStatEntry
            {
                std::string label = {};
                size_t label_hash = 0;
                ngl::fwk::GraphicsFramework::GpuScopeStatLatest stat = {};
            };
            static std::vector<UiGpuScopeStatEntry> cached_all_entries = {};
            static ngl::u64 cached_latest_frame_id = 0;

            ImGui::Checkbox("Pause Updates", &dbgw_gpu_profiler_pause_updates);
            if (!dbgw_gpu_profiler_pause_updates)
            {
                // 更新停止がOFFの間だけ最新スナップショットを取り込む.
                cached_all_entries.clear();
                cached_latest_frame_id = 0;
                auto& all_entries_ref = cached_all_entries;
                auto& latest_frame_id_ref = cached_latest_frame_id;
                gfxfw_.EnumerateLatestGpuScopeStats([&all_entries_ref, &latest_frame_id_ref](const ngl::fwk::GraphicsFramework::GpuScopeStatEntry& e)
                {
                    if (!e.stat.valid)
                    {
                        return;
                    }
                    UiGpuScopeStatEntry ui_entry = {};
                    ui_entry.label = (e.label ? e.label : "");
                    ui_entry.label_hash = std::hash<std::string>{}(ui_entry.label);
                    ui_entry.stat = e.stat;
                    latest_frame_id_ref = std::max(latest_frame_id_ref, e.stat.frame_id);
                    all_entries_ref.push_back(ui_entry);
                });
            }
            const auto& all_entries = cached_all_entries;
            const ngl::u64 latest_frame_id = cached_latest_frame_id;

            // UIの2値状態を同期する.
            if (dbgw_gpu_profiler_show_all_history)
            {
                dbgw_gpu_profiler_latest_only = false;
            }

            std::vector<UiGpuScopeStatEntry> entries = {};
            entries.reserve(all_entries.size());
            for (const auto& e : all_entries)
            {
                if (dbgw_gpu_profiler_latest_only && e.stat.frame_id != latest_frame_id)
                {
                    continue;
                }
                entries.push_back(e);
            }

            // 既定はGPUの実行順。近接tickは同一バケットとしてハッシュ順(同値時のみラベル順)へフォールバック.
            constexpr ngl::u64 k_gpu_profiler_begin_tick_bucket = 128ull;
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
            {
                if (a.stat.frame_id != b.stat.frame_id)
                {
                    return a.stat.frame_id > b.stat.frame_id;
                }
                const ngl::u64 a_bucket = a.stat.gpu_begin_tick / k_gpu_profiler_begin_tick_bucket;
                const ngl::u64 b_bucket = b.stat.gpu_begin_tick / k_gpu_profiler_begin_tick_bucket;
                if (a_bucket != b_bucket)
                {
                    return a_bucket < b_bucket;
                }
                if (a.label_hash != b.label_hash)
                {
                    return a.label_hash < b.label_hash;
                }
                if (a.label != b.label)
                {
                    return a.label < b.label;
                }
                return a.stat.gpu_begin_tick < b.stat.gpu_begin_tick;
            });
            ImGui::Text("Scope Count: %d", static_cast<int>(entries.size()));
            ImGui::Text("Latest FrameId: %llu", static_cast<unsigned long long>(latest_frame_id));
            if (ImGui::Checkbox("Show Latest Frame Only", &dbgw_gpu_profiler_latest_only))
            {
                dbgw_gpu_profiler_show_all_history = !dbgw_gpu_profiler_latest_only;
            }
            if (ImGui::Checkbox("Show All History", &dbgw_gpu_profiler_show_all_history))
            {
                dbgw_gpu_profiler_latest_only = !dbgw_gpu_profiler_show_all_history;
            }
            ImGui::Checkbox("Hierarchy View", &dbgw_gpu_profiler_hierarchy_view);
            if (ImGui::Button("Export CSV"))
            {
                // 直近統計のスナップショットをCSVへ保存.
                std::ofstream ofs("gpu_profiler_latest.csv", std::ios::out | std::ios::trunc);
                if (ofs.is_open())
                {
                    ofs << "scope,last_ms,avg_ms,p95_ms,max_ms,frame_id\n";
                    for (const auto& e : entries)
                    {
                        ofs << e.label
                            << "," << e.stat.last_ms
                            << "," << e.stat.avg_ms
                            << "," << e.stat.p95_ms
                            << "," << e.stat.max_ms
                            << "," << e.stat.frame_id
                            << "\n";
                    }
                }
            }
            ImGui::Separator();
            if (dbgw_gpu_profiler_hierarchy_view)
            {
                struct TreeNode
                {
                    std::vector<std::pair<std::string, TreeNode>> children = {};
                    const UiGpuScopeStatEntry* p_entry = nullptr;
                };
                auto FindOrAddChild = [](TreeNode& node, const std::string& name) -> TreeNode&
                {
                    for (auto& kv : node.children)
                    {
                        if (kv.first == name)
                        {
                            return kv.second;
                        }
                    }
                    node.children.push_back({name, {}});
                    return node.children.back().second;
                };
                TreeNode root = {};
                for (const auto& e : entries)
                {
                    TreeNode* p_node = &root;
                    std::string path = e.label;
                    size_t segment_begin = 0;
                    while (segment_begin <= path.size())
                    {
                        const size_t slash_pos = path.find('/', segment_begin);
                        const size_t segment_end = (slash_pos == std::string::npos) ? path.size() : slash_pos;
                        const std::string segment = path.substr(segment_begin, segment_end - segment_begin);
                        p_node = &FindOrAddChild(*p_node, segment);
                        if (slash_pos == std::string::npos)
                        {
                            break;
                        }
                        segment_begin = slash_pos + 1;
                    }
                    p_node->p_entry = &e;
                }

                std::function<void(const TreeNode&, const std::string&)> DrawTree;
                DrawTree = [&DrawTree](const TreeNode& node, const std::string& name)
                {
                    if (name.empty())
                    {
                        for (const auto& kv : node.children)
                        {
                            DrawTree(kv.second, kv.first);
                        }
                        return;
                    }

                    const bool is_leaf = node.children.empty();
                    ImGuiTreeNodeFlags flags = is_leaf ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen) : ImGuiTreeNodeFlags_None;
                    const bool opened = ImGui::TreeNodeEx(name.c_str(), flags);
                    if (node.p_entry)
                    {
                        const auto& s = node.p_entry->stat;
                        ImGui::SameLine();
                        ImGui::Text("last %.4f / avg %.4f / p95 %.4f / max %.4f / frame %llu",
                            s.last_ms, s.avg_ms, s.p95_ms, s.max_ms, static_cast<unsigned long long>(s.frame_id));
                    }
                    if (!is_leaf && opened)
                    {
                        for (const auto& kv : node.children)
                        {
                            DrawTree(kv.second, kv.first);
                        }
                        ImGui::TreePop();
                    }
                };

                DrawTree(root, "");
            }
            else if (ImGui::BeginTable("GpuScopeTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Scope");
                ImGui::TableSetupColumn("Last [ms]");
                ImGui::TableSetupColumn("Avg [ms]");
                ImGui::TableSetupColumn("P95 [ms]");
                ImGui::TableSetupColumn("Max [ms]");
                ImGui::TableSetupColumn("FrameId");
                ImGui::TableHeadersRow();

                for (const auto& e : entries)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(e.label.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.4f", e.stat.last_ms);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.4f", e.stat.avg_ms);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.4f", e.stat.p95_ms);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.4f", e.stat.max_ms);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%llu", static_cast<unsigned long long>(e.stat.frame_id));
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
#endif

    const auto dlit_dir = ngl::math::Vec3::Normalize(ngl::math::Mat33::RotAxisY(dbgw_dlit_angle_h) * ngl::math::Mat33::RotAxisX(dbgw_dlit_angle_v) * (-ngl::math::Vec3::UnitY()));

    // オブジェクト移動.
    if (true)
    {
        for (int i = 0; i < test_move_mesh_entity_array_.size(); ++i)
        {
            auto* e = test_move_mesh_entity_array_[i];
            if (nullptr == e)
                continue;

            float move_range      = (i % 10) / 10.0f;
            const float sin_curve = sinf((float)app_sec_ * 2.0f * ngl::math::k_pi_f * 0.1f * (move_range + 1.0f));

            auto tr    = e->GetTransform();
            auto trans = tr.GetColumn3();
            trans.z += sin_curve * delta_sec * 5.0f;
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

    if (sw_tess_update_tessellation_frame_toggle)
    {
        // 1FでOFFにする.
        sw_tess_update_tessellation = false;
    }

    // 描画用シーン情報.
    ngl::gfx::SceneRepresentation frame_scene;
    {
        for (auto& e : mesh_entity_array_)
        {
            if (nullptr == e.get())
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
        new_render_param->prev_camera_pos = prev_camera_pos_;
        new_render_param->prev_camera_pose = prev_camera_pose_;

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

    if (benchmark_enabled_)
    {
        ++benchmark_app_frame_counter_;
        const double frame_fps = (delta_sec > 1e-6f) ? (1.0 / static_cast<double>(delta_sec)) : 0.0;
        const double frame_delta_ms = static_cast<double>(delta_sec) * 1000.0;
        const double moving_avg_delta_ms = moving_avg_t50_frame_sec_ * 1000.0;
        const double delta_stable_threshold_ms = std::max(1.0, moving_avg_delta_ms * 0.20);
        const bool min_ready_time_elapsed = app_sec_ >= benchmark_min_ready_seconds_;
        if (min_ready_time_elapsed && std::abs(frame_delta_ms - moving_avg_delta_ms) <= delta_stable_threshold_ms)
        {
            ++benchmark_delta_stable_counter_;
        }
        else
        {
            benchmark_delta_stable_counter_ = 0;
        }

        const bool warmup_frame_ready = benchmark_app_frame_counter_ > benchmark_warmup_frames_;
        const bool warmup_delta_ready = benchmark_delta_stable_counter_ >= benchmark_ready_delta_stable_frames_;
        const bool start_timeout_reached = app_sec_ >= benchmark_start_timeout_seconds_;
        if (start_timeout_reached && !warmup_delta_ready && !benchmark_start_timeout_logged_)
        {
            benchmark_start_timeout_logged_ = true;
            std::cout << "[Benchmark] Start timeout reached. Bypassing delta-stability gate at app_sec=" << app_sec_ << std::endl;
        }

        if (warmup_frame_ready && (warmup_delta_ready || start_timeout_reached))
        {
            if (!benchmark_measure_started_logged_)
            {
                benchmark_measure_started_logged_ = true;
                benchmark_measure_start_frame_ = benchmark_app_frame_counter_;
                benchmark_measure_start_app_sec_ = app_sec_;
                benchmark_measure_start_reason_ = warmup_delta_ready ? "delta_stable" : "start_timeout";
                std::cout << "[Benchmark] Measure start at frame=" << benchmark_app_frame_counter_
                          << " app_sec=" << app_sec_
                          << " reason=" << benchmark_measure_start_reason_
                          << " fps=" << frame_fps
                          << " delta_ms=" << frame_delta_ms << std::endl;
            }
            if (CaptureBenchmarkFrameStats())
            {
                ++benchmark_captured_frame_counter_;
            }
        }

        if (benchmark_captured_frame_counter_ >= benchmark_measure_frames_)
        {
            SaveBenchmarkReport();
            return false;
        }
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

    auto calc_view_proj = [](const ngl::math::Vec3& camera_pos, const ngl::math::Mat33& camera_pose, float camera_fov_y, float screen_aspect_ratio)
    {
        struct ViewProjPair
        {
            ngl::math::Mat34 view;
            ngl::math::Mat44 proj;
        };

        ViewProjPair out{};
        out.view = ngl::math::CalcViewMatrix(camera_pos, camera_pose.GetColumn2(), camera_pose.GetColumn1());
        out.proj = ngl::math::CalcReverseInfiniteFarPerspectiveMatrix(camera_fov_y, screen_aspect_ratio, 0.1f);
        return out;
    };

    const float screen_aspect_ratio = (float)screen_width / (float)screen_height;
    const auto prev_view_proj = calc_view_proj(render_param_->prev_camera_pos, render_param_->prev_camera_pose, render_param_->camera_fov_y, screen_aspect_ratio);

    // Raytracing Scene更新.
    if (rt_scene_.IsValid())
    {
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

            render_frame_desc.camera_pos   = render_param_->camera_pos;
            render_frame_desc.camera_pose  = render_param_->camera_pose;
            render_frame_desc.camera_fov_y = render_param_->camera_fov_y;
            render_frame_desc.prev_view_mat = prev_view_proj.view;
            render_frame_desc.prev_proj_mat = prev_view_proj.proj;

            render_frame_desc.p_scene = &render_param_->frame_scene;

            render_frame_desc.feature_config.lighting.directional_light_dir       = render_param_->dlight_dir;
            render_frame_desc.feature_config.lighting.directional_light_intensity = dbgw_dlit_intensity;
            render_frame_desc.feature_config.lighting.sky_light_intensity         = dbgw_skylight_intensity;
            render_frame_desc.feature_config.material_debug.debug_emissive_enable = dbgw_material_debug_emissive_enable;
            render_frame_desc.feature_config.material_debug.debug_emissive_intensity = dbgw_material_debug_emissive_intensity;
            render_frame_desc.feature_config.material_debug.debug_emissive_mask_albedo_color = dbgw_material_debug_emissive_mask_albedo_color;
            render_frame_desc.feature_config.material_debug.debug_emissive_mask_tolerance = dbgw_material_debug_emissive_mask_tolerance;

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
            render_frame_desc.prev_view_mat = prev_view_proj.view;
            render_frame_desc.prev_proj_mat = prev_view_proj.proj;

            render_frame_desc.p_scene = &render_param_->frame_scene;

            // Config.
            {
                // Lighting.
                {
                    render_frame_desc.feature_config.lighting.directional_light_dir       = render_param_->dlight_dir;
                    render_frame_desc.feature_config.lighting.directional_light_intensity = dbgw_dlit_intensity;
                    render_frame_desc.feature_config.lighting.sky_light_intensity         = dbgw_skylight_intensity;
                }
                // Material Debug.
                {
                    render_frame_desc.feature_config.material_debug.debug_emissive_enable = dbgw_material_debug_emissive_enable;
                    render_frame_desc.feature_config.material_debug.debug_emissive_intensity = dbgw_material_debug_emissive_intensity;
                    render_frame_desc.feature_config.material_debug.debug_emissive_mask_albedo_color = dbgw_material_debug_emissive_mask_albedo_color;
                    render_frame_desc.feature_config.material_debug.debug_emissive_mask_tolerance = dbgw_material_debug_emissive_mask_tolerance;
                }
                // GI.
                {
                    if (instant_rdv_.IsValid())
                    {
                        render_frame_desc.feature_config.gi.p_instant_rdv                             = &instant_rdv_;
                        render_frame_desc.feature_config.gi.sample_mode                        = dbgw_gi_sample_mode;
                        render_frame_desc.feature_config.gi.enable_sky_visibility              = dbgw_enable_instant_rdv_sky_visibility_lighting;
                        render_frame_desc.feature_config.gi.enable_radiance                    = dbgw_enable_instant_rdv_radiance_lighting;
                        render_frame_desc.feature_config.gi.probe_sample_offset_view           = dbgw_gi_probe_sample_offset_view;
                        render_frame_desc.feature_config.gi.probe_sample_offset_surface_normal = dbgw_gi_probe_sample_offset_surface_normal;
                        render_frame_desc.feature_config.gi.probe_sample_offset_bent_normal    = dbgw_gi_probe_sample_offset_bent_normal;

                        render_frame_desc.feature_config.gi.enable_instant_rdv_all_injection_pass = dbgw_enable_instant_rdv_all_injection_pass;
                        render_frame_desc.feature_config.gi.enable_instant_rdv_all_removal_pass = dbgw_enable_instant_rdv_all_removal_pass;
                        render_frame_desc.feature_config.gi.enable_instant_rdv_main_view_injection_pass = dbgw_enable_instant_rdv_main_view_injection_pass;
                        render_frame_desc.feature_config.gi.enable_instant_rdv_main_view_removal_pass = dbgw_enable_instant_rdv_main_view_removal_pass;
                        render_frame_desc.feature_config.gi.enable_instant_rdv_shadow_view_injection_pass = dbgw_enable_instant_rdv_shadow_view_injection_pass;
                        render_frame_desc.feature_config.gi.enable_instant_rdv_shadow_view_removal_pass = dbgw_enable_instant_rdv_shadow_view_removal_pass;
                    }
                }
                // GTAO.
                {
                    render_frame_desc.feature_config.gtao_demo.enable = dbgw_enable_gtao_demo;
                }
            }

            if (dbgw_enable_raytrace_pass && rt_scene_.IsValid())
            {
                // デバッグメニューからRaytracePassの有無切り替え.
                render_frame_desc.p_rt_scene = &rt_scene_;
            }

            render_frame_desc.ref_test_tex_srv      = res_texture_->ref_view_;
            render_frame_desc.h_prev_lit            = h_prev_light;  // MainViewはヒストリ有効.
            render_frame_desc.h_other_graph_out_tex = subview_render_frame_out.h_propagate_lit;

            // Debug.
            {
                render_frame_desc.debug_multithread_render_pass    = dbgw_multithread_render_pass;
                render_frame_desc.debug_multithread_cascade_shadow = dbgw_multithread_cascade_shadow;

                render_frame_desc.debugview_halfdot_gray              = dbgw_view_half_dot_gray;
                render_frame_desc.debugview_enable_feedback_blur_test = dbgw_enable_feedback_blur_test;
                render_frame_desc.debugview_subview_result            = dbgw_enable_sub_view_path;
                render_frame_desc.debugview_raytrace_result           = dbgw_enable_raytrace_pass;

                render_frame_desc.debugview_instant_rdv_sky_visibility = dbgw_view_instant_rdv_sky_visibility;

                render_frame_desc.debugview_gbuffer               = dbgw_view_gbuffer;
                render_frame_desc.debugview_dshadow               = dbgw_view_dshadow;
                render_frame_desc.debugview_general_debug_buffer  = dbgw_view_general_debug_buffer;
                render_frame_desc.debugview_general_debug_channel = dbgw_view_general_debug_channel;
                render_frame_desc.debugview_general_debug_rate    = dbgw_view_general_debug_rate;
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
    const bool shift_pressed = window.Dep().GetVirtualKeyState()[VK_SHIFT] != 0;
    if (shift_pressed && !prev_shift_)
    {
        slow_camera_control_ = !slow_camera_control_;
    }
    prev_shift_ = shift_pressed;

    const float camera_control_speed_scale = slow_camera_control_ ? 0.25f : 1.0f;
    float camera_translate_speed = dbgw_camera_speed * camera_control_speed_scale;

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
                const auto rot_rad = ngl::math::k_pi_f * mouse_diff * 0.001f * camera_control_speed_scale;
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

int main(int argc, char** argv)
{
    std::cout << "Boot App" << std::endl;
    ngl::time::Timer::Instance().StartTimer("AppGameTime");
    ParseCommandLineArgs(argc, argv);

    {
        std::unique_ptr<ngl::boot::BootApplication> boot(ngl::boot::BootApplication::Create());
        AppGame app;
        boot->Run(&app);
    }

    std::cout << "App Time: " << ngl::time::Timer::Instance().GetElapsedSec("AppGameTime") << std::endl;
    return 0;
}

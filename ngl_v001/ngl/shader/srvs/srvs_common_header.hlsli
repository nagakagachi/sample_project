#ifndef NGL_SHADER_SRVS_COMMON_HEADER_H
#define NGL_SHADER_SRVS_COMMON_HEADER_H
#include "../include/ngl_shader_config.hlsli"

#if 0

srvs_common_header.hlsli

hlsl/C++共通ヘッダ.

cppからインクルードする場合は以下のマクロ定義を先行定義すること.
    #define NGL_SHADER_CPP_INCLUDE


参考資料
https://github.com/dubiousconst282/VoxelRT
https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/
https://github.com/cgyurgyik/fast-voxel-traversal-algorithm/blob/master/overview/FastVoxelTraversalOverview.md

#endif


// OctahedralMapストレージ版のSSPの球面/半球面モード切り替え.
//   SHストレージ版は別のマクロでモード切替しているため注意.
//   現状はSHストレージ版をメインで検証しているためリファレンスとして維持.
#ifndef NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
#define NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE 1
#endif

// BrickLocalAABB の比較用コンパイル時切り替え.
// 0: 従来どおり Brick 全域を leaf 走査, 1: Brick 内 occupied 範囲だけへ走査を絞る.
//      複数ScreenのInjectionをする場合に, 前のScreenのInjectionで真のAABBが変わっても次のScreenのRemovalに反映されない, 許容可能そうであるが, 要確認.
#ifndef NGL_SRVS_ENABLE_BRICK_LOCAL_AABB
#define NGL_SRVS_ENABLE_BRICK_LOCAL_AABB 1
#endif

// BBV trace 呼び出し箇所ごとの比較用切り替え. 現状のHiBricｋTraceは通常版に比べてオーバヘッドが大きく負荷増加してしまうため修正中.
// 0: 通常 trace_bbv, 1: trace_bbv_hibrick
#ifndef NGL_SRVS_TRACE_USE_HIBRICK_BBV_REMOVAL_LIST_BUILD
#define NGL_SRVS_TRACE_USE_HIBRICK_BBV_REMOVAL_LIST_BUILD 1
#endif
#ifndef NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_DIRECT_SH_UPDATE
#define NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_DIRECT_SH_UPDATE 0
#endif
#ifndef NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_UPDATE
#define NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_UPDATE 0
#endif
#ifndef NGL_SRVS_TRACE_USE_HIBRICK_FSP_ELEMENT_UPDATE
#define NGL_SRVS_TRACE_USE_HIBRICK_FSP_ELEMENT_UPDATE 0
#endif
#ifndef NGL_SRVS_TRACE_USE_HIBRICK_FSP_VISIBLE_SURFACE_ELEMENT_UPDATE
#define NGL_SRVS_TRACE_USE_HIBRICK_FSP_VISIBLE_SURFACE_ELEMENT_UPDATE 0
#endif


#ifdef NGL_SHADER_CPP_INCLUDE
    using uint = ngl::u32;
    using uint4 = ngl::math::Vec4u;
    using uint3 = ngl::math::Vec3u;
    using uint2 = ngl::math::Vec2u;
    using int4 = ngl::math::Vec4i;
    using int3 = ngl::math::Vec3i;
    using int2 = ngl::math::Vec2i;
    using float4 = ngl::math::Vec4;
    using float3 = ngl::math::Vec3;
    using float2 = ngl::math::Vec2;

    using float3x4 = ngl::math::Mat34;
    using float4x4 = ngl::math::Mat44;
    #define NGL_CPP_ALIGN_16 alignas(16)
#else
    #define NGL_CPP_ALIGN_16
#endif


    // シェーダとCppで一致させる.
    // BBV本体バッファは
    //   1. Brickごとの 8x8x8 occupancy bitmask 領域
    //   2. Brickごとの補助データ領域
    //   3. HiBrickごとの集約データ領域
    // の順で連続配置する。
    //
    // ここではその各領域サイズ計算に使う共通定数だけを置く。
    // Bbv単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_bbv_per_voxel_resolution (8)
    // 1 Brick あたりの fine voxel 数.
    #define k_bbv_per_voxel_bitmask_bit_count (k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution)
    // fine voxel bitmask を u32 配列へ詰めた時の要素数.
    #define k_bbv_per_voxel_bitmask_u32_count ((k_bbv_per_voxel_bitmask_bit_count + 31) / 32)
    // Brick 内 bitmask アクセスは 4x4x4 の SubBrick 単位で扱う。
    // 8x8x8 Brick は 2x2x2 個の SubBrick へ分割され、各 SubBrick は 64bit で表現される。
    #define k_bbv_subbrick_resolution (4)
    #define k_bbv_subbrick_bit_count (k_bbv_subbrick_resolution * k_bbv_subbrick_resolution * k_bbv_subbrick_resolution)
    #define k_bbv_subbrick_per_voxel_axis_count (k_bbv_per_voxel_resolution / k_bbv_subbrick_resolution)
    #define k_bbv_subbrick_per_voxel_count (k_bbv_subbrick_per_voxel_axis_count * k_bbv_subbrick_per_voxel_axis_count * k_bbv_subbrick_per_voxel_axis_count)
    #define k_bbv_subbrick_per_voxel_resolution_vec3i int3(k_bbv_subbrick_per_voxel_axis_count, k_bbv_subbrick_per_voxel_axis_count, k_bbv_subbrick_per_voxel_axis_count)

    #define NGL_SRVS_BBV_SUBBRICK_BIT_LAYOUT_LINEAR (0)
    #define NGL_SRVS_BBV_SUBBRICK_BIT_LAYOUT_MORTON (1)
    // 4x4x4 SubBrick 内 64bit の配置を切り替える. 0: XYZ linear, 1: Morton 3D.
    #ifndef NGL_SRVS_BBV_SUBBRICK_BIT_LAYOUT
    #define NGL_SRVS_BBV_SUBBRICK_BIT_LAYOUT NGL_SRVS_BBV_SUBBRICK_BIT_LAYOUT_MORTON
    #endif

    #if (k_bbv_per_voxel_resolution != (k_bbv_subbrick_resolution * k_bbv_subbrick_per_voxel_axis_count))
        #error "k_bbv_per_voxel_resolution must be an integer multiple of k_bbv_subbrick_resolution."
    #endif
    // Brick data region の 1 Brick あたり要素数.
    // occupied voxel count / work(last visible frame) に加え、
    // 比較用に BrickLocalAABB を有効化した場合は packed min/max を保持する。
    #if NGL_SRVS_ENABLE_BRICK_LOCAL_AABB
        #define k_bbv_brick_local_aabb_data_u32_count (2)
    #else
        #define k_bbv_brick_local_aabb_data_u32_count (0)
    #endif
    #define k_bbv_brick_data_u32_count (2 + k_bbv_brick_local_aabb_data_u32_count)
    // HiBrick は 2x2x2 Brick cluster を 1 単位とする。
    #define k_bbv_hibrick_brick_resolution (2)
    // HiBrick data region の 1 HiBrick あたり要素数.
    // 初段階では cluster 内 occupied voxel 総数のみを持つ。
    #define k_bbv_hibrick_data_u32_count (1)
    // Brick radiance accumulation buffer は RGB + sample count の 4 要素を 1 Brick ごとに持つ。
    #define k_bbv_radiance_accum_component_count (4)
    // HDR radiance の atomic 加算用 fixed-point スケール.
    #define k_bbv_radiance_fixed_point_scale (256.0)
    // 極端な HDR 値による uint overflow を避けるための入力 clamp.
    #define k_bbv_radiance_input_clamp (64.0)
    // BBV radiance injection は 2x2 スクリーンタイル group ごとに 1F で 1 tile を処理し、4F で全更新する。
    // dispatch も group 単位に圧縮し、未選択 tile の threadgroup は起動しない前提の固定設定。
    #define k_bbv_radiance_injection_tile_width (16)
    #define k_bbv_radiance_injection_tile_group_resolution (2)
    #define k_bbv_radiance_injection_phase_count (k_bbv_radiance_injection_tile_group_resolution * k_bbv_radiance_injection_tile_group_resolution)
    // BBV radiance resolve は 2x2x2 Brick group ごとに 1F で 1 Brick を処理し、8F で全更新する。
    // dispatch 自体も group 単位に圧縮し、未選択 Brick を起動しない前提の固定設定。
    #define k_bbv_radiance_resolve_brick_group_resolution (2)
    #define k_bbv_radiance_resolve_phase_count (k_bbv_radiance_resolve_brick_group_resolution * k_bbv_radiance_resolve_brick_group_resolution * k_bbv_radiance_resolve_brick_group_resolution)


    #define k_bbv_per_voxel_resolution_inv (1.0 / float(k_bbv_per_voxel_resolution))
    #define k_bbv_per_voxel_resolution_vec3i int3(k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution)

    // fsp probeあたりのOctahedralMapAtlas解像度.
    #define k_fsp_probe_octmap_width (8)
    // 旧 border 前提コード互換用エイリアス。現在は border なしの 8x8 をそのまま使う。
    #define k_fsp_probe_octmap_width_with_border (k_fsp_probe_octmap_width)
    // fsp
    #define k_fsp_probe_distance_max (50.0)
    // fsp
    #define k_fsp_probe_distance_max_inv (1.0 / k_fsp_probe_distance_max)

    
    // Bbv 全体更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT 60
    // Bbv 可視Fsp要素更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define BBV_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT 0
    
    // Fsp 全体更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define FSP_ALL_ELEMENT_UPDATE_SKIP_COUNT 60
    // Fsp 可視Fsp要素更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define FSP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT 1

    // 非可視表面Voxel除去用スタックの1要素のコンポーネント数.
    #define k_component_count_RemoveVoxelList 4

    // ScreenSpaceProbe情報テクスチャ群のフル解像度に対する縮小率.
    #define SCREEN_SPACE_PROBE_INFO_DOWNSCALE 8
    // 1ProbeあたりのOctahedralMapAtlas解像度.
    #define SCREEN_SPACE_PROBE_OCT_RESOLUTION 8
    #define SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV (1.0 / float(SCREEN_SPACE_PROBE_OCT_RESOLUTION))
    #define SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT (SCREEN_SPACE_PROBE_OCT_RESOLUTION * SCREEN_SPACE_PROBE_OCT_RESOLUTION)
    // Temporal Filterの棄却パラメータ.
    #define SCREEN_SPACE_PROBE_TEMPORAL_FILTER_NORMAL_COS_THRESHOLD 0.95
    #define SCREEN_SPACE_PROBE_TEMPORAL_FILTER_PLANE_DIST_THRESHOLD 0.25

    // Spatial Filterの棄却パラメータ.
    //#define SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD cos(2e-2 * 3.141592) // GI-1.0
    #define SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD 0.5
    #define SCREEN_SPACE_PROBE_SPATIAL_FILTER_DEPTH_EXP_SCALE 32.0

    // SideCacheの棄却パラメータ.
    #define SCREEN_SPACE_PROBE_SIDE_CACHE_PLANE_THRESHOLD 0.25

    // SsProbe PreUpdateの再配置確率.
    #define SCREEN_SPACE_PROBE_PREUPDATE_RELOCATION_PROBABILITY 0.025


    // シェーダとCppで一致させる.
    // Voxel追加データバッファ. Bbv一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct BbvOptionalData
    {
        // ジオメトリ表面を含むBrickまでの相対ベクトル. ジオメトリ表面を含むBrickは0, それ以外はマンハッタン距離.
        int3 to_surface_vector;
        uint dummy0;

        // Screen-space から Resolve した Brick radiance.
        float3 resolved_radiance;
        // Resolve に寄与した sample count.
        uint resolved_sample_count;
    };


    // FrustumSurfaceProbeのデータ.
    struct FspProbeData
    {
        uint probe_offset_v3;//signed 10bit vector3 encode. Bbv上でのプローブ埋まり回避のためのオフセット.
        uint atomic_work;// 可視要素リスト作成時の重複除去用.
        
        float avg_sky_visibility;
        uint probe_data_dummy;
    };

    static const uint k_fsp_invalid_probe_index = ~uint(0);
    static const uint k_fsp_probe_flag_allocated = 1u << 0;
    static const uint k_fsp_max_cascade_count = 8u;

    // FSP V1 lifecycle 用の probe pool エントリ.
    // cell 側は probe index だけを持ち、probe 側に状態を寄せる。
    struct FspProbePoolData
    {
        uint owner_cell_index;
        uint probe_offset_v3;// signed 10bit vector3 encode.
        uint last_seen_frame;
        uint flags;

        float avg_sky_visibility;
        uint last_update_frame;
        uint debug_last_observed_frame;
        uint debug_last_released_frame;
    };

    // 可視サーフェイス情報Injection用のView情報.
    // DepthBuffer等からInjectionする際のそのBufferのView情報を格納.
    struct BbvSurfaceInjectionViewInfo
    {
        float3x4 cb_view_mtx;
        float3x4 cb_view_inv_mtx;
        float4x4 cb_proj_mtx;
        float4x4 cb_proj_inv_mtx;

        // 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化. xは平行投影もサポートするために利用.
        //	for calc_view_z_from_ndc_z(ndc_z, cb_ndc_z_to_view_z_coef)
        float4	cb_ndc_z_to_view_z_coef;

        // xy: ターゲットDepthBuffer上のオフセット, zw: サイズ.
        int4    cb_view_depth_buffer_offset_size;
    };


    struct SrvsToroidalGridParam
    {
        int3 grid_resolution;
        float cell_size;

        float3 grid_min_pos;
        float cell_size_inv;

        int3 grid_min_voxel_coord;
        int flatten_2d_width;// GridCell要素を2Dにフラット化する際の幅.

        int3 grid_toroidal_offset;
        int dummy0;

        int3 grid_toroidal_offset_prev;
        int dummy1;

        int3 grid_move_cell_delta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
        int dummy2;
    };

    struct NGL_CPP_ALIGN_16 FspCascadeGridParam
    {
        SrvsToroidalGridParam grid;
        uint cell_offset;
        uint cell_count;
        uint dummy0;
        uint dummy1;
    };

    // Dispatchパラメータ.
    // ConstantBuffer は HLSL の cbuffer packing に合わせて 16-byte 単位で member レイアウトを調整すること。
    // 特に次のルールを守る:
    // - float3 / int3 の次には同一 16-byte レジスタを埋める scalar を置く
    // - 16-byte をまたぐ位置に int2 / float2 / 構造体 / 配列を置かない
    // - member 追加時は C++ / HLSL の両方で同じ並びになるよう明示的に padding を入れる
    // - 「sizeof が 16 の倍数」だけでは不十分で、各 member の開始オフセットも HLSL 側と一致させる
    struct NGL_CPP_ALIGN_16 SrvsParam
    {
        // bitmask brick voxel関連パラメータ.
        SrvsToroidalGridParam bbv NGL_CPP_MEMBER_INIT({});
        int3 bbv_indirect_cs_thread_group_size NGL_CPP_MEMBER_INIT({});// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        int bbv_visible_voxel_buffer_size NGL_CPP_MEMBER_INIT({});// 更新プローブ用のワークサイズ.
        int bbv_hollow_voxel_buffer_size NGL_CPP_MEMBER_INIT({});// 削除用中空Voxel情報のワークサイズ.
        int dummy0 NGL_CPP_MEMBER_INIT({});
        int dummy1 NGL_CPP_MEMBER_INIT({});
        int dummy2 NGL_CPP_MEMBER_INIT({});

        // Temporal再利用重みの最小値.
        float ss_probe_temporal_min_hysteresis NGL_CPP_MEMBER_INIT({0.7f});
        // Temporal再利用重みの最大値.
        float ss_probe_temporal_max_hysteresis NGL_CPP_MEMBER_INIT({0.98f});
        // Temporal再投影有効化フラグ.
        int ss_probe_temporal_reprojection_enable NGL_CPP_MEMBER_INIT({1});
        // RayGuiding有効化フラグ.
        int ss_probe_ray_guiding_enable NGL_CPP_MEMBER_INIT({1});

        // SideCache有効化フラグ.
        int ss_probe_side_cache_enable NGL_CPP_MEMBER_INIT({1});
        // SideCache参照を許可する最大経過フレーム.
        int ss_probe_side_cache_max_life_frame NGL_CPP_MEMBER_INIT({24});
        // Probe位置再選択確率.
        float ss_probe_preupdate_relocation_probability NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_PREUPDATE_RELOCATION_PROBABILITY)});
        // TemporalFilter 法線Cos閾値.
        float ss_probe_temporal_filter_normal_cos_threshold NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_TEMPORAL_FILTER_NORMAL_COS_THRESHOLD)});
        // TemporalFilter 平面距離閾値.
        float ss_probe_temporal_filter_plane_dist_threshold NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_TEMPORAL_FILTER_PLANE_DIST_THRESHOLD)});
        // SideCache 平面距離閾値.
        float ss_probe_side_cache_plane_dist_threshold NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_SIDE_CACHE_PLANE_THRESHOLD)});
        
        // 1Fに一つだけ更新するProbeグループのサイズ. 1で毎フレーム更新, 2で2x2のProbeグループのうち1Fで一つだけ更新.
        int ss_probe_temporal_update_group_size NGL_CPP_MEMBER_INIT({1});
        // SSプローブ更新時のレイ開始オフセットスケール. 単位はBbvセル幅. sqrt(3.0f).
        float ss_probe_ray_start_offset_scale NGL_CPP_MEMBER_INIT({1.732050808f});
        // SSプローブ更新時のレイ始点法線オフセットスケール. 単位はBbvセル幅.
        float ss_probe_ray_normal_offset_scale NGL_CPP_MEMBER_INIT({0.2f});
        // SpatialFilter 法線Cos閾値.
        float ss_probe_spatial_filter_normal_cos_threshold NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD)});

        // SpatialFilter 深度差重み影響度.
        float ss_probe_spatial_filter_depth_exp_scale NGL_CPP_MEMBER_INIT({float(SCREEN_SPACE_PROBE_SPATIAL_FILTER_DEPTH_EXP_SCALE)});
        int dummy3_2 NGL_CPP_MEMBER_INIT({0});
        int dummy3_3 NGL_CPP_MEMBER_INIT({0});
        int dummy3_4 NGL_CPP_MEMBER_INIT({0});
        int2 dummy3_5_6 NGL_CPP_MEMBER_INIT({});// fsp開始を16byte alignに揃えるためのパディング.

        // FSP ClipMap cascade情報.
        FspCascadeGridParam fsp_cascade[k_fsp_max_cascade_count] NGL_CPP_MEMBER_INIT({});
        // IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        int3 fsp_indirect_cs_thread_group_size NGL_CPP_MEMBER_INIT({});
        // 更新プローブ用のワークサイズ.
        int fsp_visible_voxel_buffer_size NGL_CPP_MEMBER_INIT({});
        int fsp_probe_pool_size NGL_CPP_MEMBER_INIT({});
        int fsp_active_probe_buffer_size NGL_CPP_MEMBER_INIT({});
        int fsp_lighting_interpolation_enable NGL_CPP_MEMBER_INIT({1});
        int fsp_spawn_front_cell_enable NGL_CPP_MEMBER_INIT({0});
        int fsp_lighting_stochastic_sampling_enable NGL_CPP_MEMBER_INIT({0});
        int fsp_cascade_count NGL_CPP_MEMBER_INIT({1});
        int fsp_total_cell_count NGL_CPP_MEMBER_INIT({0});
        int fsp_probe_atlas_tile_width NGL_CPP_MEMBER_INIT({0});
        int fsp_probe_atlas_tile_height NGL_CPP_MEMBER_INIT({0});
        int debug_fsp_probe_cascade NGL_CPP_MEMBER_INIT({-1});
        int2 fsp_dummy_padding1_2 NGL_CPP_MEMBER_INIT({});

        // MainViewのDepthBuffer解像度.
        int2 tex_main_view_depth_size NGL_CPP_MEMBER_INIT({});
        uint frame_count NGL_CPP_MEMBER_INIT({0});
        int debug_view_sub_mode NGL_CPP_MEMBER_INIT({0});

        float3 main_light_dir_ws NGL_CPP_MEMBER_INIT({});

        int debug_view_category NGL_CPP_MEMBER_INIT({-1});
        
        int debug_bbv_probe_mode NGL_CPP_MEMBER_INIT({-1});
        int debug_fsp_probe_mode NGL_CPP_MEMBER_INIT({-1});

        float debug_probe_radius NGL_CPP_MEMBER_INIT({0.0f});
        float debug_probe_near_geom_scale NGL_CPP_MEMBER_INIT({0.2f});
    };
#ifdef NGL_SHADER_CPP_INCLUDE
    // C++用のコンパイル時定数デフォルト構造体.
    inline constexpr SrvsParam k_default_srvs_param{};
#endif

#ifdef NGL_SHADER_CPP_INCLUDE
    static_assert((sizeof(SrvsToroidalGridParam) % 16) == 0, "SrvsToroidalGridParam size must be 16-byte aligned");
    static_assert((sizeof(SrvsParam) % 16) == 0, "SrvsParam size must be 16-byte aligned");
    static_assert((offsetof(SrvsParam, tex_main_view_depth_size) % 16) == 0, "SrvsParam::tex_main_view_depth_size must start on a 16-byte boundary");
#endif


#endif // NGL_SHADER_SRVS_COMMON_HEADER_H

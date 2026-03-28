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


// ScreenSpaceProbe OctMap mode switch.
// SsProbeのキャプチャを球面か半球面のどちらで実行するか切り替え. 半球面は向きが低解像度法線によるノイズの影響が大きいため, 現状は球面で検証中.
// 0: spherical octahedral map, 1: hemispherical octahedral map.
#ifndef NGL_SSP_HEMI_OCTMAP
#define NGL_SSP_HEMI_OCTMAP 0
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
#endif


    // シェーダとCppで一致させる.
    // Bbv単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_bbv_common_data_u32_count (2)
    // Bbv単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_bbv_per_voxel_resolution (8)
    #define k_bbv_per_voxel_bitmask_bit_count (k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution)
    #define k_bbv_per_voxel_bitmask_u32_count ((k_bbv_per_voxel_bitmask_bit_count + 31) / 32)
    // k_bbv_per_voxel_bitmask_u32_count == 16 なら それぞれのu32コンポーネントの非ゼロフラグを管理する16bitをマスクする定義.
    #define k_bbv_per_voxel_bitmask_u32_component_mask ((1 << k_bbv_per_voxel_bitmask_u32_count) - 1)
    // Bbv単位が持つデータサイズ(u32単位).
    #define k_bbv_per_voxel_u32_count (k_bbv_per_voxel_bitmask_u32_count + k_bbv_common_data_u32_count)


    #define k_bbv_per_voxel_resolution_inv (1.0 / float(k_bbv_per_voxel_resolution))
    #define k_bbv_per_voxel_resolution_vec3i int3(k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution)

    // probeあたりのOctahedralMapAtlas解像度.
    #define k_probe_octmap_width (6)
    // それぞれのOctMapの+側境界に1テクセルボーダーを追加することで全方向に1テクセルのマージンを確保する.
    #define k_probe_octmap_width_with_border (k_probe_octmap_width + 2)
    // probeあたりのOctMapテクセル数.
    #define k_per_probe_texel_count (k_probe_octmap_width * k_probe_octmap_width)

    #define k_wcp_probe_distance_max (50.0)
    #define k_wcp_probe_distance_max_inv (1.0 / k_wcp_probe_distance_max)

    
    // Bbv 全体更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define BBV_ALL_ELEMENT_UPDATE_SKIP_COUNT 60
    // Bbv 可視Wcp要素更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define BBV_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT 0
    
    // Wcp 全体更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define WCP_ALL_ELEMENT_UPDATE_SKIP_COUNT 60
    // Wcp 可視Wcp要素更新のフレーム負荷軽減用スキップ数. 0: スキップせずに1Fで全要素処理. 1: 1つ飛ばしでスキップ(半分).
    #define WCP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT 1

    // 非可視表面Voxel除去用スタックの1要素のコンポーネント数.
    #define k_component_count_RemoveVoxelList 4

    // ScreenSpaceProbeタイルサイズ. 1ProbeあたりのOctahedralMapAtlasの幅.
    #define SCREEN_SPACE_PROBE_TILE_SIZE 8
    #define SCREEN_SPACE_PROBE_TILE_SIZE_INV (1.0 / float(SCREEN_SPACE_PROBE_TILE_SIZE))
    #define SCREEN_SPACE_PROBE_TILE_TEXEL_COUNT (SCREEN_SPACE_PROBE_TILE_SIZE * SCREEN_SPACE_PROBE_TILE_SIZE)


    // Temporal Filterの棄却パラメータ.
    #define SCREEN_SPACE_PROBE_TEMPORAL_FILTER_NORMAL_COS_THRESHOLD 0.2
    #define SCREEN_SPACE_PROBE_TEMPORAL_FILTER_PLANE_DIST_THRESHOLD 0.25

    // Spatial Filterの棄却パラメータ.
    //#define SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD cos(2e-2 * 3.141592) // GI-1.0
    #define SCREEN_SPACE_PROBE_SPATIAL_FILTER_NORMAL_COS_THRESHOLD 0.2
    #define SCREEN_SPACE_PROBE_SPATIAL_FILTER_DEPTH_EXP_SCALE 32.0

    // SideCacheの棄却パラメータ.
    #define SCREEN_SPACE_PROBE_SIDE_CACHE_PLANE_THRESHOLD 0.25

    // SsProbe PreUpdateの再配置確率.
    #define SCREEN_SPACE_PROBE_PREUPDATE_RELOCATION_PROBABILITY 0.05


    // シェーダとCppで一致させる.
    // Voxel追加データバッファ. Bbv一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct BbvOptionalData
    {
        // ジオメトリ表面を含むBrickまでの相対ベクトル. ジオメトリ表面を含むBrickは0, それ以外はマンハッタン距離.
        int3 to_surface_vector;
        // 現在未使用.
        uint dummy;
    };


    // WorldCacheProbeのデータ.
    struct WcpProbeData
    {
        uint probe_offset_v3;//signed 10bit vector3 encode. Bbv上でのプローブ埋まり回避のためのオフセット.
        uint atomic_work;// 可視要素リスト作成時の重複除去用.
        
        float avg_sky_visibility;
        uint probe_data_dummy;
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

    // Dispatchパラメータ.
    // 16byte align でmenberレイアウトを調整すること.
    struct SrvsParam
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
        float ss_probe_temporal_min_hysteresis NGL_CPP_MEMBER_INIT({0.85f});
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
        int2 dummy3_5_6 NGL_CPP_MEMBER_INIT({});// wcp開始を16byte alignに揃えるためのパディング.

        SrvsToroidalGridParam wcp NGL_CPP_MEMBER_INIT({});
        // IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        int3 wcp_indirect_cs_thread_group_size NGL_CPP_MEMBER_INIT({});
        // 更新プローブ用のワークサイズ.
        int wcp_visible_voxel_buffer_size NGL_CPP_MEMBER_INIT({});

        // MainViewのDepthBuffer解像度.
        int2 tex_main_view_depth_size NGL_CPP_MEMBER_INIT({});
        uint frame_count NGL_CPP_MEMBER_INIT({0});
        int dummy4_2 NGL_CPP_MEMBER_INIT({0});

        float3 main_light_dir_ws NGL_CPP_MEMBER_INIT({});

        int debug_view_mode NGL_CPP_MEMBER_INIT({-1});
        
        int debug_bbv_probe_mode NGL_CPP_MEMBER_INIT({-1});
        int debug_wcp_probe_mode NGL_CPP_MEMBER_INIT({-1});

        float debug_probe_radius NGL_CPP_MEMBER_INIT({0.0f});
        float debug_probe_near_geom_scale NGL_CPP_MEMBER_INIT({0.2f});
    };

#ifdef NGL_SHADER_CPP_INCLUDE
    static_assert((sizeof(SrvsToroidalGridParam) % 16) == 0, "SrvsToroidalGridParam size must be 16-byte aligned");
    static_assert((sizeof(SrvsParam) % 16) == 0, "SrvsParam size must be 16-byte aligned");
#endif


#endif // NGL_SHADER_SRVS_COMMON_HEADER_H
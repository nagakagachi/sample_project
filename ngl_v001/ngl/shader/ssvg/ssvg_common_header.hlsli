#ifndef NGL_SHADER_SSVG_COMMON_HEADER_H
#define NGL_SHADER_SSVG_COMMON_HEADER_H
#include "../include/ngl_shader_config.hlsli"

#if 0

ssvg_common_header.hlsli

hlsl/C++共通ヘッダ.

cppからインクルードする場合は以下のマクロ定義を先行定義すること.
    #define NGL_SHADER_CPP_INCLUDE


参考資料
https://github.com/dubiousconst282/VoxelRT
https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/
https://github.com/cgyurgyik/fast-voxel-traversal-algorithm/blob/master/overview/FastVoxelTraversalOverview.md

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
    // BitmaskBrickVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_bbv_common_data_u32_count (2)
    // BitmaskBrickVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_bbv_per_voxel_resolution (8)
    #define k_bbv_per_voxel_bitmask_bit_count (k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution)
    #define k_bbv_per_voxel_bitmask_u32_count ((k_bbv_per_voxel_bitmask_bit_count + 31) / 32)
    // k_bbv_per_voxel_bitmask_u32_count == 16 なら それぞれのu32コンポーネントの非ゼロフラグを管理する16bitをマスクする定義.
    #define k_bbv_per_voxel_bitmask_u32_component_mask ((1 << k_bbv_per_voxel_bitmask_u32_count) - 1)
    // BitmaskBrickVoxel単位が持つデータサイズ(u32単位).
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


    // シェーダとCppで一致させる.
    // Voxel追加データバッファ. BitmaskBrickVoxel一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct BbvOptionalData
    {
        // ジオメトリ表面を含むBrickまでの相対ベクトル. ジオメトリ表面を含むBrickは0, それ以外はマンハッタン距離.
        int3 to_surface_vector;

        // BitmaskBrickVoxel内部でのプローブ候補位置を表す線形インデックス. 0は無効, probe_pos_code-1 が実際のインデックス. 値域は 0,k_bbv_per_voxel_bitmask_bit_count.
        uint probe_pos_code;
    };


    // WorldCacheProbeのデータ.
    struct WcpProbeData
    {
        uint probe_offset_v3;//signed 10bit vector3 encode.
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
    };


    struct SsvgToroidalGridParam
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
    struct SsvgParam
    {
        // bitmask brick voxel関連パラメータ.
        SsvgToroidalGridParam bbv;
        int3 bbv_indirect_cs_thread_group_size;// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        int bbv_visible_voxel_buffer_size;// 更新プローブ用のワークサイズ.
        int bbv_hollow_voxel_buffer_size;// 削除用中空Voxel情報のワークサイズ.

        int dummy0;
        int dummy1;
        int dummy2;

        SsvgToroidalGridParam wcp;
        int3 wcp_indirect_cs_thread_group_size;// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        int wcp_visible_voxel_buffer_size;// 更新プローブ用のワークサイズ.

        int2 tex_hw_depth_size;
        uint frame_count;
        int dummy3;

        int debug_view_mode;
        
        int debug_bbv_probe_mode;
        int debug_wcp_probe_mode;

        float debug_probe_radius;
        float debug_probe_near_geom_scale;
    };


#endif // NGL_SHADER_SSVG_COMMON_HEADER_H
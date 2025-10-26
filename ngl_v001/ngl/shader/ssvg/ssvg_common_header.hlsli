#ifndef NGL_SHADER_SSVG_COMMON_HEADER_H
#define NGL_SHADER_SSVG_COMMON_HEADER_H

#if 0

ssvg_common_header.hlsli

hlsl/C++共通ヘッダ.

cppからインクルードする場合は以下のマクロ定義を先行定義すること.
    #define NGL_SHADER_CPP_INCLUDE

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
#endif


    // シェーダとCppで一致させる.
    // BitmaskBrickVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_bbv_common_data_u32_count (1)
    // BitmaskBrickVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_bbv_per_voxel_resolution (8)
    #define k_bbv_per_voxel_bitmask_bit_count (k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution*k_bbv_per_voxel_resolution)
    #define k_bbv_per_voxel_bitmask_u32_count ((k_bbv_per_voxel_bitmask_bit_count + 31) / 32)
    // BitmaskBrickVoxel単位のデータサイズ(u32単位)
    #define k_bbv_per_voxel_u32_count (k_bbv_per_voxel_bitmask_u32_count + k_bbv_common_data_u32_count)

    #define k_bbv_per_voxel_resolution_inv (1.0 / float(k_bbv_per_voxel_resolution))
    #define k_bbv_per_voxel_resolution_vec3i int3(k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution)

    // probeあたりのOctMap解像度.
    #define k_probe_octmap_width (6)
    // それぞれのOctMapの+側境界に1テクセルボーダーを追加することで全方向に1テクセルのマージンを確保する.
    #define k_probe_octmap_width_with_border (k_probe_octmap_width+2)

    #define k_per_probe_texel_count (k_probe_octmap_width*k_probe_octmap_width)

    // 可視Probe更新時のスキップ数. 0でスキップせずに可視Probeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT 0
    // Probe全体更新のスキップ数. 0でスキップせずにProbeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_ALL_PROBE_SKIP_COUNT 16
    
    // WCP Probe全体更新のスキップ数. 0でスキップせずにProbeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define WCP_FRAME_PROBE_UPDATE_SKIP_COUNT 0


    // シェーダとCppで一致させる.
    // Voxel追加データバッファ. BitmaskBrickVoxel一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct BbvOptionalData
    {
        // ジオメトリ表面を含むVoxelまでの距離. ジオメトリ表面を含むVoxelは0, それ以外はマンハッタン距離.
        int3 surface_distance;

        // BitmaskBrickVoxel内部でのプローブ候補位置を表す線形インデックス. 0は無効, probe_pos_code-1 が実際のインデックス. 値域は 0,k_bbv_per_voxel_bitmask_bit_count.
        uint probe_pos_code;
    };


    // WorldCacheProbeのデータ.
    struct WcpProbeData
    {
        float4 data;
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

        SsvgToroidalGridParam wcp;

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
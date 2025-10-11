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
    // ObmVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_obm_common_data_u32_count (1)
    // ObmVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_obm_per_voxel_resolution (8)
    #define k_obm_per_voxel_bitmask_bit_count (k_obm_per_voxel_resolution*k_obm_per_voxel_resolution*k_obm_per_voxel_resolution)
    #define k_obm_per_voxel_occupancy_bitmask_u32_count ((k_obm_per_voxel_bitmask_bit_count + 31) / 32)
    // ObmVoxel単位のデータサイズ(u32単位)
    #define k_obm_per_voxel_u32_count (k_obm_per_voxel_occupancy_bitmask_u32_count + k_obm_common_data_u32_count)

    #define k_obm_per_voxel_resolution_inv (1.0 / float(k_obm_per_voxel_resolution))
    #define k_obm_per_voxel_resolution_vec3i int3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution)

    // probeあたりのOctMap解像度.
    #define k_probe_octmap_width (6)
    // それぞれのOctMapの+側境界に1テクセルボーダーを追加することで全方向に1テクセルのマージンを確保する.
    #define k_probe_octmap_width_with_border (k_probe_octmap_width+2)

    #define k_per_probe_texel_count (k_probe_octmap_width*k_probe_octmap_width)

    // 可視Probe更新時のスキップ数. 0でスキップせずに可視Probeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT 0
    // Probe全体更新のスキップ数. 0でスキップせずにProbeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_ALL_PROBE_SKIP_COUNT 16


    // シェーダとCppで一致させる.
    // CoarseVoxelバッファ. ObmVoxel一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct ObmVoxelOptionalData
    {
        // ObmVoxel内部でのプローブ位置の線形インデックス. 0は無効, probe_pos_code-1 が実際のインデックス. 値域は 0,k_obm_per_voxel_bitmask_bit_count.
        uint probe_pos_code;
        // 占有された表面Voxelまでの距離の格納, 更新.
        uint distance_to_surface_voxel;
    };


    // Dispatchパラメータ.
    struct SsvgParam
    {
        int3 base_grid_resolution;
        uint flag;

        float3 grid_min_pos;
        float cell_size;
        int3 grid_toroidal_offset;
        float cell_size_inv;

        int3 grid_toroidal_offset_prev;
        int dummy0;
        
        int3 grid_move_cell_delta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
        int probe_atlas_texture_base_width;// probeのAtlasを配置するテクスチャの基準幅. 実際はProbe毎のAtlasサイズを乗じたサイズのテクスチャを扱う.


        int3 voxel_dispatch_thread_group_count;// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
        
        int update_probe_work_count;// 更新プローブ用のワークサイズ.

        int2 tex_hw_depth_size;
        uint frame_count;

        int debug_view_mode;
        int debug_probe_mode;
        float debug_probe_radius;
        float debug_probe_near_geom_scale;
    };


#endif // NGL_SHADER_SSVG_COMMON_HEADER_H
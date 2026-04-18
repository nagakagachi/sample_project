#if 0

ss_probe_direct_sh_update_cs.hlsl

DirectSH方式 SkyVisibility 更新.

OctahedralMap テクスチャを持たず, L1 SH (float4: Y00, Y1_{-1}(y), Y1_0(z), Y1_{+1}(x)) を直接保持する検証パス.
1 ThreadGroup = 1 ProbeタイルTile (8x8 = 64 threads).
Dispatch は 1/8 解像度 (TileInfo と同サイズ).

処理フロー:
  1. スレッド0: DirectSH TileInfo から probe WS 位置/法線を共有メモリへ書き込む.
     Preupdate で計算済みの Best Prev Tile をテクスチャから読み取る.
  2. 全スレッド: 担当 gindex の OctMap セル方向 (dir_ws) を計算.
  3. Best Prev Tile の History SH から dir_ws への評価値 = gs_temporal_reprojected_value[gindex].
  4. RayGuiding CDF 構築 (gs_prev_radiance ベース).
  5. CDF 逆変換サンプリング → BitmaskBrickVoxel レイトレース → sky_visibility.
  6. Temporal Blend で ss_blended_value[gindex] を決定.
  7. blended_value から L1 SH を積分し RWScreenSpaceProbeDirectSHTex へ書き出す.

#endif

#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/rand_util.hlsli"

// SHストレージ版のサンプリングを半球モードにするか.
#ifndef NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE
#define NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE 1
#endif

#if !defined(NGL_SSP_RAY_GUIDING_ENABLE)
#define NGL_SSP_RAY_GUIDING_ENABLE 1
#endif

#if !defined(NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS)
#define NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS 0.03
#endif

// スレッド
#define THREAD_GROUP_OCTAHEDRAL_MAP_WIDTH (SCREEN_SPACE_PROBE_OCT_RESOLUTION)
#define THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT (THREAD_GROUP_OCTAHEDRAL_MAP_WIDTH*THREAD_GROUP_OCTAHEDRAL_MAP_WIDTH)

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

// ---- 共有メモリ ----
groupshared float  gs_probe_hw_depth;
groupshared float3 gs_probe_pos_ws;
groupshared float3 gs_probe_approx_normal_ws;

groupshared uint   gs_ray_sample_accum[THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT * 2]; // [i*2+0]=count, [i*2+1]=sum_visibility_fixed
groupshared float  gs_prev_radiance[THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT];
groupshared float  gs_guiding_cdf[THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT];
groupshared uint   gs_temporal_best_prev_tile_packed;                         // 0xffffffff = 無効 (Preupdateで計算済み)
groupshared float  gs_temporal_reprojected_value[THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT];          // 前フレームSHをセル値に展開

// ---- SH Parallel Reduction 用 ----
groupshared float4 gs_sh_reduce[THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT];

// ---- ユーティリティ ----
float biased_shadow_temporal_weight(float curr, float prev)
{
    // https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf
    const float l1 = curr, l2 = prev;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    alpha = clamp(alpha, 0.0, 0.95);
    return alpha * alpha;
}


[numthreads(SCREEN_SPACE_PROBE_OCT_RESOLUTION, SCREEN_SPACE_PROBE_OCT_RESOLUTION, 1)]
void main_cs(
    uint3 dtid   : SV_DispatchThreadID,
    uint3 gtid   : SV_GroupThreadID,
    uint gindex  : SV_GroupIndex,
    uint3 gid    : SV_GroupID
)
{
    // Dispatch は 1/8 解像度 (TileInfo と同サイズ) でかける.
    uint2 tile_tex_size;
    ScreenSpaceProbeDirectSHTileInfoTex.GetDimensions(tile_tex_size.x, tile_tex_size.y);
    if(any(gid.xy >= tile_tex_size))
        return;

    const int2 probe_tile_id = int2(gid.xy);
    const int2 probe_atlas_local_pos = int2(gtid.xy);

    // RandomInstance
    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(float(probe_tile_id.x * SCREEN_SPACE_PROBE_OCT_RESOLUTION + gtid.x),
                                                      float(probe_tile_id.y * SCREEN_SPACE_PROBE_OCT_RESOLUTION + gtid.y),
                                                      float(cb_srvs.frame_count))));

    // ---- Step 1: スレッド0 が TileInfo + BestPrevTile を読み込んで共有メモリに展開 ----
    const float4 tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(probe_tile_id, 0));

    if(0 == gindex)
    {
        gs_probe_hw_depth         = 1.0;
        gs_probe_pos_ws           = float3(0,0,0);
        gs_probe_approx_normal_ws = float3(0,0,1);
        gs_temporal_best_prev_tile_packed = ScreenSpaceProbeDirectSHBestPrevTileTex.Load(int3(probe_tile_id, 0)).x;
        gs_sh_reduce[0] = float4(0, 0, 0, 0);

        if(isValidDepth(tile_info.x))
        {
            const uint2  depth_size = cb_ngl_sceneview.cb_render_resolution;
            const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
            const int2   probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(tile_info.y);
            const int2   probe_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_INFO_DOWNSCALE + probe_pos_in_tile;
            const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * depth_size_inv;

            const float view_z = calc_view_z_from_ndc_z(tile_info.x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pos_vs = CalcViewSpacePosition(probe_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pos_vs, 1.0));

            gs_probe_hw_depth         = tile_info.x;
            gs_probe_pos_ws           = pos_ws;
            gs_probe_approx_normal_ws = OctDecode(tile_info.zw);
        }
    }
    // 共有メモリクリア.
    {
        gs_ray_sample_accum[gindex * 2 + 0] = 0;
        gs_ray_sample_accum[gindex * 2 + 1] = 0;
        gs_prev_radiance[gindex] = 0.0;
        gs_guiding_cdf[gindex]   = 0.0;
        gs_temporal_reprojected_value[gindex] = 0.0;
    }
    GroupMemoryBarrierWithGroupSync();

    // 無効 tile は早期リターン (SH を 0 クリアして出力).
    if(!isValidDepth(gs_probe_hw_depth))
    {
        if(0 == gindex)
            RWScreenSpaceProbeDirectSHTex[probe_tile_id] = float4(0, 0, 0, 0);
        return;
    }

    const float3 base_normal_ws = gs_probe_approx_normal_ws;
    float3 base_tangent_ws, base_bitangent_ws;
    BuildOrthonormalBasis(base_normal_ws, base_tangent_ws, base_bitangent_ws);

    // ---- Step 2: 全スレッドが担当セルの方向を計算 ----
    const float2 cell_oct_uv = (float2(probe_atlas_local_pos) + 0.5) * SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
    #if NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE
        const float3 cell_dir_ws = OctahedralDecodeHemisphereDirWs(cell_oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);
    #else
        const float3 cell_dir_ws = OctahedralDecodeSphereDirWs(cell_oct_uv);
    #endif

    // ---- Step 3: Preupdateで計算済みのBestPrevTile から History SH を展開 ----
    if(0xffffffff != gs_temporal_best_prev_tile_packed)
    {
        const int2 best_prev_tile = SspUnpackTileId(gs_temporal_best_prev_tile_packed);
        const float4 prev_sh = ScreenSpaceProbeDirectSHHistoryTex.Load(int3(best_prev_tile, 0));
        // SkyVisibility として評価するため saturate.
        gs_temporal_reprojected_value[gindex] = saturate(max(0.0, dot(prev_sh, EvaluateL1ShBasis(cell_dir_ws))));
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 4: gs_prev_radiance を gs_temporal_reprojected_value から計算 ----
    {
        const float temporal_val_for_guiding = (0 == cb_srvs.ss_probe_ray_guiding_enable) ? 0.0 : gs_temporal_reprojected_value[gindex];
        const float clamped_normal_dot = max(0.0, dot(base_normal_ws, cell_dir_ws));
        gs_prev_radiance[gindex] = (temporal_val_for_guiding + NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS) * clamped_normal_dot;
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 5: CDF 構築 (スレッド0) ----
    if(0 == gindex)
    {
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0; i < THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT; ++i)
        {
            cdf_sum += gs_prev_radiance[i];
            gs_guiding_cdf[i] = cdf_sum;
        }
        const float cdf_inv = 1.0 / max(cdf_sum, 1e-6);
        [unroll]
        for(uint i = 0; i < THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT; ++i)
            gs_guiding_cdf[i] *= cdf_inv;
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 6: レイトレース ----
    const float ray_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_normal_offset_scale;
    const float3 ray_origin_base = gs_probe_pos_ws + base_normal_ws * ray_normal_offset;

    const uint ray_count = THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT;
    for(int sample_index = 0; sample_index < 1; ++sample_index)
    {
        const uint ray_index = gindex + ray_count * sample_index;
            
    #if NGL_SSP_RAY_GUIDING_ENABLE
        const float guiding_rand = rng.rand();
        uint selected_oct_cell_index = gindex & (THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT - 1);
        [unroll]
        for(uint i = 0; i < THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT; ++i)
        {
            if(guiding_rand <= gs_guiding_cdf[i])
            {
                selected_oct_cell_index = i;
                break;
            }
        }
        const uint2 selected_cell = uint2(selected_oct_cell_index % SCREEN_SPACE_PROBE_OCT_RESOLUTION, selected_oct_cell_index / SCREEN_SPACE_PROBE_OCT_RESOLUTION);
        const float2 jitter = rng.rand2();
        const float2 selected_oct_uv = (float2(selected_cell) + jitter) * SCREEN_SPACE_PROBE_OCT_RESOLUTION_INV;
        #if NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE
            float3 sample_ray_dir = OctahedralDecodeHemisphereDirWs(selected_oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);
        #else
            float3 sample_ray_dir = OctahedralDecodeSphereDirWs(selected_oct_uv);
        #endif
    #else
        const float3 unit_v3 = random_unit_vector3(float3(probe_tile_id.x, probe_tile_id.y, rng.rand()));
        const float3 local_dir = normalize(unit_v3 + float3(0, 0, 1));
        float3 sample_ray_dir = local_dir.x * base_tangent_ws + local_dir.y * base_bitangent_ws + local_dir.z * base_normal_ws;
    #endif
        const float3 ray_origin = ray_origin_base + sample_ray_dir * ray_start_offset;

        int hit_voxel_index = -1;
        float4 debug_ray_info;
#if NGL_SRVS_TRACE_USE_HIBRICK_SS_PROBE_DIRECT_SH_UPDATE
        float4 curr_ray_t = trace_bbv_hibrick(
            hit_voxel_index, debug_ray_info,
            ray_origin, sample_ray_dir, 30.0,
            cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
            cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);
#else
        float4 curr_ray_t = trace_bbv(
            hit_voxel_index, debug_ray_info,
            ray_origin, sample_ray_dir, 30.0,
            cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
            cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);
#endif

        const float sky_vis = (curr_ray_t.x < 0.0) ? 1.0 : 0.0;
        // 担当セルへ蓄積.
        #if NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE
            const float2 hit_oct_uv = OctahedralEncodeHemisphereDirWs(sample_ray_dir, base_normal_ws);// 半球版.
        #else
            const float2 hit_oct_uv = OctahedralEncodeSphereDirWs(sample_ray_dir);
        #endif

        const int2   hit_cell = clamp(int2(hit_oct_uv * SCREEN_SPACE_PROBE_OCT_RESOLUTION), int2(0,0), int2(SCREEN_SPACE_PROBE_OCT_RESOLUTION-1, SCREEN_SPACE_PROBE_OCT_RESOLUTION-1));
        const int    hit_index = hit_cell.y * SCREEN_SPACE_PROBE_OCT_RESOLUTION + hit_cell.x;
        InterlockedAdd(gs_ray_sample_accum[hit_index * 2 + 0], 1u);
        InterlockedAdd(gs_ray_sample_accum[hit_index * 2 + 1], uint(sky_vis));
    }
    GroupMemoryBarrierWithGroupSync();

    // 最新のサンプルによるOctahedralMap → SH投影値を直接計算.
    {
        const uint   hit_count = gs_ray_sample_accum[gindex * 2 + 0];
        // ヒットなしセルは temporal 再投影値にフォールバック (Octahedral モードと同等).
        const float  current_value = (hit_count > 0) ? (float(gs_ray_sample_accum[gindex * 2 + 1]) / float(hit_count)) : gs_temporal_reprojected_value[gindex];

        // ---- per-cell Temporal Blend (SH投影前) ----
        float blended_value = current_value;
        const bool has_temporal = (0xffffffff != gs_temporal_best_prev_tile_packed)
                               && (0 != cb_srvs.ss_probe_temporal_reprojection_enable);
        if(has_temporal)
        {
            const float prev_val = gs_temporal_reprojected_value[gindex];
            float temporal_rate = biased_shadow_temporal_weight(current_value, prev_val);
            temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);
            blended_value = lerp(current_value, prev_val, temporal_rate);
        }

        // ---- SH 積分 (全スレッド並列 SH投影 + Parallel Reduction) ----
#if NGL_SSP_DIRECT_SH_SAMPLE_HEMISPHERE
        const float solid_angle = (2.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#else
        const float solid_angle = (4.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_OCT_TEXEL_COUNT);
#endif
        // 各スレッドが自身のセルの SH 投影値を計算 (cell_dir_ws は Step 2 で計算済み).
        gs_sh_reduce[gindex] = (solid_angle * blended_value) * EvaluateL1ShBasis(cell_dir_ws);
    }
    GroupMemoryBarrierWithGroupSync();

    // Parallel Reduction (log2(64) = 6 steps).
    [unroll]
    for(uint stride = THREAD_GROUP_OCTAHEDRAL_MAP_CELL_COUNT / 2; stride > 0; stride >>= 1)
    {
        if(gindex < stride)
            gs_sh_reduce[gindex] += gs_sh_reduce[gindex + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if(0 == gindex)
    {
        float4 sh_out = gs_sh_reduce[0];

        // Output.
        RWScreenSpaceProbeDirectSHTex[probe_tile_id] = sh_out;
    }
}

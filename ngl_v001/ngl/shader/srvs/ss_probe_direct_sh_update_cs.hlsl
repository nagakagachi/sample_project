#if 0

ss_probe_direct_sh_update_cs.hlsl

DirectSH方式 SkyVisibility 更新.

OctahedralMap テクスチャを持たず, L1 SH (float4: l00,l1x,l1y,l1z) を直接保持する検証パス.
1 ThreadGroup = 1 ProbeタイルTile (8x8 = 64 threads).
Dispatch は 1/8 解像度 (TileInfo と同サイズ).

処理フロー:
  1. スレッド0: DirectSH TileInfo から probe WS 位置/法線を共有メモリへ書き込む.
  2. 全スレッド: 担当 gindex の OctMap セル方向 (dir_ws) を計算.
  3. 簡易 Temporal 再投影: 現フレームプローブ位置を前フレームスクリーンに再投影して
     対応 History Tileを特定, History SH から dir_ws への評価値 = ss_prev_radiance[gindex].
  4. RayGuiding CDF 構築 (ss_prev_radiance ベース).
  5. CDF 逆変換サンプリング → BitmaskBrickVoxel レイトレース → sky_visibility.
  6. Temporal Blend で ss_blended_value[gindex] を決定.
  7. blended_value から L1 SH を積分し RWScreenSpaceProbeDirectSHTex へ書き出す.

#endif

#include "srvs_util.hlsli"
#include "../include/scene_view_struct.hlsli"
#include "../include/rand_util.hlsli"

#if !defined(NGL_SSP_RAY_GUIDING_ENABLE)
#define NGL_SSP_RAY_GUIDING_ENABLE 1
#endif

#if !defined(NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS)
#define NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS 0.03
#endif

#ifndef NGL_SSP_RAY_COUNT
#define NGL_SSP_RAY_COUNT (SCREEN_SPACE_PROBE_TILE_SIZE * SCREEN_SPACE_PROBE_TILE_SIZE)
#endif


#define NGL_SSP_DIRECT_SH_TEMPORAL_FILTER_SH 1


ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

// ---- 共有メモリ ----
groupshared float  gs_probe_hw_depth;
groupshared float3 gs_probe_pos_ws;
groupshared float3 gs_probe_approx_normal_ws;

groupshared uint   gs_ray_sample_accum[NGL_SSP_RAY_COUNT * 2]; // [i*2+0]=count, [i*2+1]=sum_visibility_fixed
groupshared float  gs_prev_radiance[NGL_SSP_RAY_COUNT];
groupshared float  gs_blended_value[NGL_SSP_RAY_COUNT];
groupshared float  gs_guiding_cdf[NGL_SSP_RAY_COUNT];
groupshared float  gs_guiding_total_weight;
groupshared uint   gs_temporal_best_prev_tile_packed; // 0xffffffff = 無効

// ---- SH 積算用 (float として InterlockedAdd で利用するため uint に bit-reinterpret) ----
groupshared uint   gs_sh_accum[4]; // l00, l1x, l1y, l1z (asuint/asfloat 変換)

// ---- ユーティリティ ----
float biased_shadow_temporal_weight(float curr, float prev)
{
    const float l1 = curr, l2 = prev;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    alpha = clamp(alpha, 0.0, 0.95);
    return alpha * alpha;
}

float2 CalcPrevUvFromWorldPos(float3 pos_ws, out bool is_valid)
{
    const float3 prev_vs = mul(cb_ngl_sceneview.cb_prev_view_mtx, float4(pos_ws, 1.0));
    const float4 prev_cs = mul(cb_ngl_sceneview.cb_prev_proj_mtx, float4(prev_vs, 1.0));
    if(abs(prev_cs.w) <= 1e-6) { is_valid = false; return float2(0,0); }
    const float2 ndc = prev_cs.xy / prev_cs.w;
    const float2 uv  = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    is_valid = all(uv >= 0.0) && all(uv <= 1.0);
    return uv;
}

// float を uint ビット列として InterlockedAdd するヘルパー (fixed-point 近似).
// 精度が十分でないため, スレッド0による逐次積算に置き換えている (gs_blended_value を使用).
// こちらは使用しないが参考として残す.

[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
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
    rng.rngState = asuint(noise_float_to_float(float3(float(probe_tile_id.x * SCREEN_SPACE_PROBE_TILE_SIZE + gtid.x),
                                                      float(probe_tile_id.y * SCREEN_SPACE_PROBE_TILE_SIZE + gtid.y),
                                                      float(cb_srvs.frame_count))));

    // ---- Step 1: スレッド0 が TileInfo を読み込んで共有メモリに展開 ----
    const float4 tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(probe_tile_id, 0));

    if(0 == gindex)
    {
        gs_probe_hw_depth         = 1.0;
        gs_probe_pos_ws           = float3(0,0,0);
        gs_probe_approx_normal_ws = float3(0,0,1);
        gs_temporal_best_prev_tile_packed = 0xffffffff;
        gs_guiding_total_weight   = 0.0;
        gs_sh_accum[0] = 0; gs_sh_accum[1] = 0; gs_sh_accum[2] = 0; gs_sh_accum[3] = 0;

        if(isValidDepth(tile_info.x))
        {
            const uint2  depth_size = cb_ngl_sceneview.cb_render_resolution;
            const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
            const int2   probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(tile_info.y);
            const int2   probe_texel_pos = probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_pos_in_tile;
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
        gs_blended_value[gindex] = 1.0; // デフォルト: 空として扱う.
        gs_guiding_cdf[gindex]   = 0.0;
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
    const float2 cell_oct_uv = (float2(probe_atlas_local_pos) + 0.5) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
    const float3 cell_dir_ws = SspDecodeDirByNormal(cell_oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);

    // ---- Step 3: 簡易 Temporal 再投影 (スレッド0 のみ対応 TileId を決定) ----
    if(0 == gindex)
    {
        bool is_valid_prev_uv;
        const float2 prev_uv = CalcPrevUvFromWorldPos(gs_probe_pos_ws, is_valid_prev_uv);
        if(is_valid_prev_uv && (0 != cb_srvs.ss_probe_temporal_reprojection_enable))
        {
            const uint2 depth_size = cb_ngl_sceneview.cb_render_resolution;
            const int tile_size = SCREEN_SPACE_PROBE_TILE_SIZE;
            const int2 probe_tile_count = max((int2(depth_size) + tile_size - 1) / tile_size, int2(1, 1));
            const int2 prev_center_tile = clamp(int2(prev_uv * float2(depth_size)) / tile_size, int2(0, 0), probe_tile_count - 1);

            const float4 prev_tile_info = ScreenSpaceProbeDirectSHHistoryTileInfoTex.Load(int3(prev_center_tile, 0));
            if(isValidDepth(prev_tile_info.x))
            {
                const int2 prev_probe_texel = prev_center_tile * tile_size + SspTileInfoDecodeProbePosInTile(prev_tile_info.y);
                const float prev_view_z = calc_view_z_from_ndc_z(prev_tile_info.x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
                const float3 prev_pos_vs = CalcViewSpacePosition((float2(prev_probe_texel) + 0.5) * cb_ngl_sceneview.cb_render_resolution_inv, prev_view_z, cb_ngl_sceneview.cb_prev_proj_mtx);
                const float3 prev_pos_ws = mul(cb_ngl_sceneview.cb_prev_view_inv_mtx, float4(prev_pos_vs, 1.0));
                const float3 diff_ws = prev_pos_ws - gs_probe_pos_ws;
                const float  plane_dist = abs(dot(diff_ws, gs_probe_approx_normal_ws));
                const float  normal_dot = dot(gs_probe_approx_normal_ws, OctDecode(prev_tile_info.zw));
                if(plane_dist < cb_srvs.ss_probe_temporal_filter_plane_dist_threshold
                    && normal_dot > cb_srvs.ss_probe_temporal_filter_normal_cos_threshold)
                {
                    gs_temporal_best_prev_tile_packed = (uint(prev_center_tile.y) << 16) | uint(prev_center_tile.x & 0xffff);
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 4: 前フレーム SH を方向ごとに評価 → ss_prev_radiance ----
    {
        float prev_val = 0.0;
        if(0xffffffff != gs_temporal_best_prev_tile_packed)
        {
            const int2 prev_tile = int2(int(gs_temporal_best_prev_tile_packed & 0xffffu), int((gs_temporal_best_prev_tile_packed >> 16) & 0xffffu));
            const float4 prev_sh = ScreenSpaceProbeDirectSHHistoryTex.Load(int3(prev_tile, 0));
            const float4 sh_basis = EvaluateL1ShBasis(cell_dir_ws);
            // SkyVisibilityとして評価するためsaturate.
            prev_val = saturate(max(0.0, dot(prev_sh, sh_basis)));
        }

        const float clamped_normal_dot = max(0.0, dot(base_normal_ws, cell_dir_ws));
        gs_prev_radiance[gindex] = (prev_val + NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS) * clamped_normal_dot;
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 5: CDF 構築 (スレッド0) ----
    if(0 == gindex)
    {
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            cdf_sum += gs_prev_radiance[i];
            gs_guiding_cdf[i] = cdf_sum;
        }
        const float cdf_inv = 1.0 / max(cdf_sum, 1e-6);
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
            gs_guiding_cdf[i] *= cdf_inv;
        gs_guiding_total_weight = cdf_sum;
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 6: レイトレース ----
    const float ray_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * cb_srvs.ss_probe_ray_normal_offset_scale;
    const float3 ray_origin_base = gs_probe_pos_ws + base_normal_ws * ray_normal_offset;

    const uint ray_count = NGL_SSP_RAY_COUNT;
    for(int sample_index = 0; sample_index < 1; ++sample_index)
    {
        const uint ray_index = gindex + ray_count * sample_index;
            
    #if NGL_SSP_RAY_GUIDING_ENABLE
        const float guiding_rand = rng.rand();
        uint selected_oct_cell_index = gindex & (NGL_SSP_RAY_COUNT - 1);
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            if(guiding_rand <= gs_guiding_cdf[i])
            {
                selected_oct_cell_index = i;
                break;
            }
        }
        const uint2 selected_cell = uint2(selected_oct_cell_index % SCREEN_SPACE_PROBE_TILE_SIZE, selected_oct_cell_index / SCREEN_SPACE_PROBE_TILE_SIZE);
        const float2 jitter = rng.rand2();
        const float2 selected_oct_uv = (float2(selected_cell) + jitter) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
        float3 sample_ray_dir = SspDecodeDirByNormal(selected_oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);
    #else
        const float3 unit_v3 = random_unit_vector3(float3(asfloat(probe_tile_id.x), asfloat(probe_tile_id.y), asfloat(gindex ^ cb_srvs.frame_count)));
        const float3 local_dir = normalize(unit_v3 + float3(0, 0, 1));
        float3 sample_ray_dir = local_dir.x * base_tangent_ws + local_dir.y * base_bitangent_ws + local_dir.z * base_normal_ws;
    #endif
        const float3 ray_origin = ray_origin_base + sample_ray_dir * ray_start_offset;

        int hit_voxel_index = -1;
        float4 debug_ray_info;
        float4 curr_ray_t = trace_bbv(
            hit_voxel_index, debug_ray_info,
            ray_origin, sample_ray_dir, 30.0,
            cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
            cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

        const float sky_vis = (curr_ray_t.x < 0.0) ? 1.0 : 0.0;
        // 担当セルへ蓄積.
        const float2 hit_oct_uv = SspEncodeDirByNormal(sample_ray_dir, base_normal_ws);
        const int2   hit_cell = clamp(int2(hit_oct_uv * SCREEN_SPACE_PROBE_TILE_SIZE), int2(0,0), int2(SCREEN_SPACE_PROBE_TILE_SIZE-1, SCREEN_SPACE_PROBE_TILE_SIZE-1));
        const int    hit_index = hit_cell.y * SCREEN_SPACE_PROBE_TILE_SIZE + hit_cell.x;
        InterlockedAdd(gs_ray_sample_accum[hit_index * 2 + 0], 1u);
        InterlockedAdd(gs_ray_sample_accum[hit_index * 2 + 1], uint(sky_vis));
    }
    GroupMemoryBarrierWithGroupSync();

    #if NGL_SSP_DIRECT_SH_TEMPORAL_FILTER_SH
        {
            const uint   hit_count = gs_ray_sample_accum[gindex * 2 + 0];
            const float  curr_vis  = (hit_count > 0) ? (float(gs_ray_sample_accum[gindex * 2 + 1]) / float(hit_count)) : 1.0;
            gs_blended_value[gindex] = curr_vis;
        }
    #else
        // ---- Step 7: Temporal Blend ----
        {
            const uint   hit_count = gs_ray_sample_accum[gindex * 2 + 0];
            const float  curr_vis  = (hit_count > 0) ? (float(gs_ray_sample_accum[gindex * 2 + 1]) / float(hit_count)) : 1.0;

            float new_vis = curr_vis;
            if(0xffffffff != gs_temporal_best_prev_tile_packed && (0 != cb_srvs.ss_probe_temporal_reprojection_enable))
            {
                // SH から再評価した前フレーム値.
                float4 prev_sh4;
                {
                    const int2 prev_tile = int2(int(gs_temporal_best_prev_tile_packed & 0xffffu), int((gs_temporal_best_prev_tile_packed >> 16) & 0xffffu));
                    prev_sh4 = ScreenSpaceProbeDirectSHHistoryTex.Load(int3(prev_tile, 0));
                }
                // SkyVisibilityとして評価するためsaturate.
                const float prev_vis = saturate(max(0.0, dot(prev_sh4, EvaluateL1ShBasis(cell_dir_ws))));
                float temporal_rate = biased_shadow_temporal_weight(curr_vis, prev_vis);
                temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);
                new_vis = lerp(new_vis, prev_vis, temporal_rate);
            }
            gs_blended_value[gindex] = new_vis;
        }
    #endif
    GroupMemoryBarrierWithGroupSync();

    // ---- Step 8: SH 積分 (スレッド0 が全セルを逐次処理) ----
    if(0 == gindex)
    {
#if NGL_SSP_HEMI_OCTMAP
        const float solid_angle = (2.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_TILE_TEXEL_COUNT);
#else
        const float solid_angle = (4.0 * 3.14159265359) / float(SCREEN_SPACE_PROBE_TILE_TEXEL_COUNT);
#endif
        float4 sh_out = float4(0, 0, 0, 0);
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            const uint2  cell = uint2(i % SCREEN_SPACE_PROBE_TILE_SIZE, i / SCREEN_SPACE_PROBE_TILE_SIZE);
            const float2 oct_uv = (float2(cell) + 0.5) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
            const float3 dir_ws = SspDecodeDirByNormal(oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);
            
            sh_out += gs_blended_value[i] * EvaluateL1ShBasis(dir_ws);
        }
        sh_out = sh_out * solid_angle;

        
        #if NGL_SSP_DIRECT_SH_TEMPORAL_FILTER_SH
            // SHでの Temporal Filter を行う. DirectSHモードはこちらで高速補間できる点を活かしたい.
            if(0xffffffff != gs_temporal_best_prev_tile_packed && (0 != cb_srvs.ss_probe_temporal_reprojection_enable))
            {
                // SH から再評価した前フレーム値.
                float4 prev_sh4;
                {
                    const int2 prev_tile = int2(int(gs_temporal_best_prev_tile_packed & 0xffffu), int((gs_temporal_best_prev_tile_packed >> 16) & 0xffffu));
                    prev_sh4 = ScreenSpaceProbeDirectSHHistoryTex.Load(int3(prev_tile, 0));
                }
                float temporal_rate = 0.5;
                temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);
                sh_out = lerp(sh_out, prev_sh4, temporal_rate);
            }
        #else
            // None. 基本使わないはず.
        #endif
        
        // Output.
        RWScreenSpaceProbeDirectSHTex[probe_tile_id] = sh_out;
    }
}

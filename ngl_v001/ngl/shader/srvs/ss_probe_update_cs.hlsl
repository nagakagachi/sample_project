
#if 0

ss_probe_update_cs.hlsl

screen-reconstructed voxel structure
ScreenSpaceProbe更新.

Temporal Reprojection + Ray Guiding.
Temporal Filter.

#endif

#include "srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

#include "../include/rand_util.hlsli"


// RayGUiding.
#if !defined( NGL_SSP_RAY_GUIDING_ENABLE )
#define NGL_SSP_RAY_GUIDING_ENABLE 1
#endif

#if !defined( NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS )
#define NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS 0.03
#endif


#ifndef NGL_SSP_RAY_COUNT
// Adjust this for ray budget per tile.
#define NGL_SSP_RAY_COUNT (SCREEN_SPACE_PROBE_TILE_SIZE * SCREEN_SPACE_PROBE_TILE_SIZE)
#endif

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;


// -------------------------------------
// Tile ThreadGroup共有のため共有メモリ.

// TileのこのフレームでのScreenSpaceProbe配置テクセル情報.
groupshared float ss_probe_hw_depth;
groupshared float ss_probe_view_z;
groupshared float3 ss_probe_pos_vs;
groupshared float3 ss_probe_pos_ws;

// ScreenSpaceProbe配置位置の近似法線情報.
groupshared float3 ss_probe_approx_normal_ws;

groupshared uint ss_ray_sample_accum[NGL_SSP_RAY_COUNT * 4];// accum_count, sky_visibility_bool,,
groupshared uint ss_temporal_best_score;
groupshared uint ss_temporal_best_prev_tile_packed;
groupshared uint ss_temporal_candidate_prev_tile_packed[NGL_SSP_RAY_COUNT];
groupshared float ss_temporal_reprojected_value[NGL_SSP_RAY_COUNT];
groupshared float ss_prev_radiance[NGL_SSP_RAY_COUNT];
groupshared float ss_guiding_cdf[NGL_SSP_RAY_COUNT];
groupshared float ss_guiding_total_weight;

// -------------------------------------

// https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf
// Algorithm 3: Biased shadow-preserving temporal hysteresis 
float biased_shadow_preserving_temporal_filter_weight(float curr_value, float prev_value)
{
    const float l1 = curr_value;
    const float l2 = prev_value;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    alpha = CalcSquare(clamp(alpha, 0.0, 0.95));// オリジナル.
    return alpha;
}

float2 CalcPrevFrameUvFromWorldPos(float3 pos_ws, out bool is_valid)
{
    const float3 prev_pos_vs = mul(cb_ngl_sceneview.cb_prev_view_mtx, float4(pos_ws, 1.0));
    const float4 prev_pos_cs = mul(cb_ngl_sceneview.cb_prev_proj_mtx, float4(prev_pos_vs, 1.0));
    if(abs(prev_pos_cs.w) <= 1e-6)
    {
        is_valid = false;
        return float2(0.0, 0.0);
    }

    const float2 prev_ndc_xy = prev_pos_cs.xy / prev_pos_cs.w;
    const float2 prev_uv = float2(prev_ndc_xy.x * 0.5 + 0.5, -prev_ndc_xy.y * 0.5 + 0.5);
    is_valid = all(prev_uv >= 0.0) && all(prev_uv <= 1.0);
    return prev_uv;
}


[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
    uint gindex : SV_GroupIndex,
	uint3 gid : SV_GroupID
)
{
    const int2 frame_skip_probe_offset = 
    int2(cb_srvs.frame_count % cb_srvs.ss_probe_temporal_update_group_size, (cb_srvs.frame_count / cb_srvs.ss_probe_temporal_update_group_size) % cb_srvs.ss_probe_temporal_update_group_size);

    const int2 probe_atlas_local_pos = gtid.xy;// タイル内でのローカル位置は時間分散でスキップされないのでそのまま.
    const int2 probe_id = gid.xy * cb_srvs.ss_probe_temporal_update_group_size + frame_skip_probe_offset;// プローブフレームスキップ考慮.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;// グローバルテクセル位置計算.
    
    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    // ScreenSpaceProbeTexelTile情報テクスチャから取得.
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id, 0));
    const float ss_probe_depth = ss_probe_tile_info.x;
    const int2 ss_probe_pos_rand_in_tile = int2(int(ss_probe_tile_info.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ss_probe_tile_info.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
    const float2 ss_probe_approx_normal_oct = ss_probe_tile_info.zw;

    uint2 depth_size = cb_ngl_sceneview.cb_render_resolution;
    const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
    
    // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
    const int2 current_probe_texel_pos = ss_probe_tile_pixel_start + ss_probe_pos_rand_in_tile;
    const float2 current_probe_texel_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;// ピクセル中心への半ピクセルオフセット考慮.
    
    // RandomInstance.
    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(global_pos.x, global_pos.y, cb_srvs.frame_count)));

    // タイルのプローブ配置情報を代表して取得.
    if(all(probe_atlas_local_pos == 0))
    {
        // クリア
        ss_probe_hw_depth = 1.0;
        ss_probe_view_z = 0.0;
        ss_probe_pos_vs = float3(0.0, 0.0, 0.0);
        ss_probe_pos_ws = float3(0.0, 0.0, 0.0);
        ss_probe_approx_normal_ws = float3(0.0, 0.0, 0.0);

        // プローブのレイ始点とするピクセルの深度取得.
        const float d = ss_probe_depth;
        if(isValidDepth(d))
        {
            const float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pixel_pos_vs = CalcViewSpacePosition(current_probe_texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

            // 事前計算した近似法線をそのまま利用.
            const float3 approx_normal_ws = OctDecode(ss_probe_approx_normal_oct);
            // タイル共有情報.
            {
                ss_probe_hw_depth = d;
                ss_probe_view_z = view_z;
                ss_probe_pos_vs = pixel_pos_vs;
                ss_probe_pos_ws = pixel_pos_ws;

                ss_probe_approx_normal_ws = approx_normal_ws;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();// タイル代表情報の共有メモリ書き込み待ち.

    // タイルが有効なプローブを持たない場合は終了.
    if(!isValidDepth(ss_probe_hw_depth))
    {
        // ミスタイルはクリアすべきか, 近傍SSプローブやワールドプローブで補填すべきか.
        RWScreenSpaceProbeTex[global_pos].r = 1.0;// ミスタイルは可視扱い.
        return;
    }

    // 作業用Sharedメモリクリア.
    {
        ss_ray_sample_accum[gindex * 4 + 0] = 0;// accum_count
        ss_ray_sample_accum[gindex * 4 + 1] = 0;// sky_visibility_bool
        ss_ray_sample_accum[gindex * 4 + 2] = 0;// unused
        ss_ray_sample_accum[gindex * 4 + 3] = 0;// unused

        ss_temporal_reprojected_value[gindex] = 0.0;
        ss_guiding_cdf[gindex] = 0.0;
    }
    ss_temporal_candidate_prev_tile_packed[gindex] = 0xffffffff;
    if(0 == gindex)
    {
        ss_temporal_best_score = 0xffffffff;
        ss_temporal_best_prev_tile_packed = 0xffffffff;
        ss_guiding_total_weight = 0.0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Temporal Reprojectionを先行して8x8値を再構成し, CDFを作る.
    {
        uint local_best_score = 0xffffffff;
        uint local_best_prev_tile_packed = 0xffffffff;

        // 3x3の9近傍を探索.
        if(gindex < 9)
        {
            bool is_valid_prev_uv;
            const float2 prev_uv = CalcPrevFrameUvFromWorldPos(ss_probe_pos_ws, is_valid_prev_uv);
            if(is_valid_prev_uv)
            {
                const int2 full_res = int2(depth_size);
                const int tile_size = SCREEN_SPACE_PROBE_TILE_SIZE;
                const int2 probe_tile_count = max((full_res + tile_size - 1) / tile_size, int2(1, 1));
                const float2 prev_pos_texel = prev_uv * float2(full_res);
                const int2 prev_center_tile = clamp(int2(prev_pos_texel) / tile_size, int2(0, 0), probe_tile_count - 1);
                const int2 candidate_offset = int2(int(gindex) % 3, int(gindex) / 3) - int2(1, 1);
                const int2 candidate_tile_id = clamp(prev_center_tile + candidate_offset, int2(0, 0), probe_tile_count - 1);
                const float4 candidate_tile_info = ScreenSpaceProbeHistoryTileInfoTex.Load(int3(candidate_tile_id, 0));

                if(isValidDepth(candidate_tile_info.x))
                {
                    // 前回プローブのワールド位置が今回プローブの位置と法線の平面から一定距離にあるかどうかで評価.
                    const int2 candidate_probe_placement_texel = candidate_tile_id * tile_size + int2(candidate_tile_info.y%tile_size, candidate_tile_info.y/tile_size);
                    const float candidate_view_z = calc_view_z_from_ndc_z(candidate_tile_info.x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
                    const float3 candidate_pos_vs = CalcViewSpacePosition((float2(candidate_probe_placement_texel) + float2(0.5, 0.5)) * depth_size_inv, candidate_view_z, cb_ngl_sceneview.cb_prev_proj_mtx);
                    const float3 candidate_pos_ws = mul(cb_ngl_sceneview.cb_prev_view_inv_mtx, float4(candidate_pos_vs, 1.0));
                    const float3 probe_pos_diff_ws = candidate_pos_ws - ss_probe_pos_ws;
                    const float plane_dist = abs(dot(probe_pos_diff_ws, ss_probe_approx_normal_ws));
                    const float probe_normal_dot = dot(ss_probe_approx_normal_ws, OctDecode(candidate_tile_info.zw));

                    // 法線での棄却を加えると完全に失敗してしまうケースが多い. AddaptiveSamplingでレイの割り当てを増やせるならありかもしれない.
                    //if((plane_dist < 5.0))// 閾値は要調整.
                    // むしろここできちんと棄却して失敗させ, Persistent Least-Recently Used (LRU) Side Cache で解決するなどした方が良いかもしれない.
                    if((plane_dist < 2.0) && (probe_normal_dot > 0.7))// 閾値は要調整.
                    {
                        //float probe_dist = length(float2(candidate_offset));// 初期実装. 安定はするが移動物体表面で適切なリプロジェクションにならずノイズになりやすい.
                        // GI-1.0はプローブ配置位置の差分を採用している. 法線の向きも評価に加えてみる.
                        float probe_dist = length(probe_pos_diff_ws) * 100.0;// ワールド長さ単位の量子化で問題ない程度にスケール.
                        probe_dist += (1.0 - probe_normal_dot) * 1.0;// 法線の向きの違いもスコアに加算.
                        
                        const uint quantized_dist = min((uint)(probe_dist * 1024.0), 0x03ffffffu);
                        local_best_score = (quantized_dist << 6) | (gindex & 0x3fu);
                        local_best_prev_tile_packed = (uint(candidate_tile_id.y) << 16) | uint(candidate_tile_id.x & 0xffff);
                    }
                }
            }
        }

        ss_temporal_candidate_prev_tile_packed[gindex] = local_best_prev_tile_packed;
        if(0xffffffff != local_best_score)
        {
            InterlockedMin(ss_temporal_best_score, local_best_score);
        }
        GroupMemoryBarrierWithGroupSync();

        if(0 == gindex)
        {
            if(0xffffffff != ss_temporal_best_score)
            {
                const uint winner_lane = ss_temporal_best_score & 0x3fu;
                ss_temporal_best_prev_tile_packed = ss_temporal_candidate_prev_tile_packed[winner_lane];
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // 再構成: グループで選択した最良タイルを全セル共通で参照.
        if(0xffffffff != ss_temporal_best_prev_tile_packed)
        {
            const int2 best_prev_tile = int2(int(ss_temporal_best_prev_tile_packed & 0xffffu), int((ss_temporal_best_prev_tile_packed >> 16) & 0xffffu));
            const int2 prev_global_pos = clamp(best_prev_tile * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos, int2(0, 0), int2(depth_size) - 1);
            const float prev_value = ScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0)).r;
            ss_temporal_reprojected_value[gindex] = prev_value;
        }
    }
    // 担当セルのOctMapベクトルとProbe面法線の内積.
    const float2 cell_oct_uv = (float2(probe_atlas_local_pos) + 0.5) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
    const float3 cell_dir_ws = SspDecodeDirByNormal(cell_oct_uv, ss_probe_approx_normal_ws);
    const float cell_octmap_normal_dot_probe_normal = max(0.0, dot(ss_probe_approx_normal_ws, cell_dir_ws));
    
    // Probe面法線での輝度評価. 面の輝度への寄与が大きいほどGuidingで誘導されるようになる.
    // バイアスを加算してから乗ずることで法線の逆向きは完全にゼロにしつつ, 順方向全体にバイアスを足す.
    const float temporal_reprojected_value_for_guiding = (0 == cb_srvs.ss_probe_ray_guiding_enable) ? 0.0 : ss_temporal_reprojected_value[gindex];
    ss_prev_radiance[gindex] = (temporal_reprojected_value_for_guiding + NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS) * cell_octmap_normal_dot_probe_normal;

    GroupMemoryBarrierWithGroupSync();

    // 8x8再構成値からCDF作成. TODO Parallel-Scanで高速化.
    if(0 == gindex)
    {
        float cdf_sum = 0.0;
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            cdf_sum += ss_prev_radiance[i];// 面法線側のサンプルを輝度に応じてウェイト付け.
            ss_guiding_cdf[i] = cdf_sum;
        }

        // NGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS によって全セルの値がバイアス分だけ増えているので, CDFの合計はNGL_SSP_RAY_GUIDING_VISIBILITY_PDF_BIAS * NGL_SSP_RAY_COUNT以上になっているはず.
        const float cdf_sum_inv = 1.0 / cdf_sum;
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            ss_guiding_cdf[i] *= cdf_sum_inv;
        }
        
        ss_guiding_total_weight = cdf_sum;
    }
    GroupMemoryBarrierWithGroupSync();

    const float3 base_normal_ws = ss_probe_approx_normal_ws;
    float3 base_tangent_ws;
    float3 base_bitangent_ws;
    BuildOrthonormalBasis(base_normal_ws, base_tangent_ws, base_bitangent_ws);

    // レイ方向オフセット. sqrt(3.0).
    const float ray_start_offset_scale = cb_srvs.ss_probe_ray_start_offset_scale;
    const float ray_origin_start_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * ray_start_offset_scale;
    // 近似法線方向オフセット.
    const float ray_origin_normal_offset_scale = cb_srvs.ss_probe_ray_normal_offset_scale;
    const float ray_origin_normal_offset = cb_srvs.bbv.cell_size * k_bbv_per_voxel_resolution_inv * ray_origin_normal_offset_scale;
    const float3 ray_origin_base = ss_probe_pos_ws + base_normal_ws * ray_origin_normal_offset;

    // レイ生成 + トレース結果をSharedに格納.
    const uint ray_count = NGL_SSP_RAY_COUNT;
    for(int sample_index = 0; sample_index < 1; ++sample_index)
    {
        const uint ray_index = gindex + ray_count * sample_index;

#if NGL_SSP_RAY_GUIDING_ENABLE
        // CDF逆変換で重要セル選択.
        // Guidingの重みの時点で面法線の逆向きはゼロになっているため, sample_ray_dirは順方向しか選択されない.
        const float guiding_rand = rng.rand();
        uint selected_oct_cell_index = ray_index & (NGL_SSP_RAY_COUNT - 1);
        [unroll]
        for(uint i = 0; i < NGL_SSP_RAY_COUNT; ++i)
        {
            if(guiding_rand <= ss_guiding_cdf[i])
            {
                selected_oct_cell_index = i;
                break;
            }
        }

        const uint2 selected_cell = uint2(selected_oct_cell_index % SCREEN_SPACE_PROBE_TILE_SIZE, selected_oct_cell_index / SCREEN_SPACE_PROBE_TILE_SIZE);
        const float2 local_cell_jitter = rng.rand2();
        const float2 selected_oct_uv = (float2(float(selected_cell.x), float(selected_cell.y)) + local_cell_jitter) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;

        float3 sample_ray_dir = SspDecodeDirByNormal(selected_oct_uv, base_tangent_ws, base_bitangent_ws, base_normal_ws);
#else
        // Cos分布半球方向ランダム.
        const float3 unit_v3 = random_unit_vector3(float3(asfloat(global_pos.x), asfloat(global_pos.y), asfloat(ray_index^cb_srvs.frame_count)));
        const float3 local_dir = normalize(unit_v3 + float3(0.0, 0.0, 1.0));
        float3 sample_ray_dir = local_dir.x * base_tangent_ws + local_dir.y * base_bitangent_ws + local_dir.z * base_normal_ws;
#endif
        const float3 sample_ray_origin = ray_origin_base + sample_ray_dir * ray_origin_start_offset;
        // タイルのスクリーンスペースプローブ位置からレイトレース.
        const float trace_distance = 30.0;
        int hit_voxel_index = -1;
        float4 debug_ray_info;
        float4 curr_ray_t_ws = 
        trace_bbv(
            hit_voxel_index, debug_ray_info,
            sample_ray_origin, sample_ray_dir, trace_distance, 
            cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
            cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

        const float sky_visibility = (0.0 > curr_ray_t_ws.x)? 1.0 : 0.0;// 負ならヒットなしで空が見えている.

        const float2 oct_uv = SspEncodeDirByNormal(sample_ray_dir, base_normal_ws);// レイ方向を法線基準のOctahedralマップUVにエンコードして格納.
        const int2 oct_cell_id = clamp(int2(oct_uv * SCREEN_SPACE_PROBE_TILE_SIZE), int2(0, 0), int2(SCREEN_SPACE_PROBE_TILE_SIZE - 1, SCREEN_SPACE_PROBE_TILE_SIZE - 1));
        const int oct_cell_index = oct_cell_id.y * SCREEN_SPACE_PROBE_TILE_SIZE + oct_cell_id.x;
        
        // Result Accumulation.
        InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 0], 1);// accum_count
        InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 1], uint(sky_visibility));// sky_visibility_bool
    }
    GroupMemoryBarrierWithGroupSync();

    uint hit_count = ss_ray_sample_accum[gindex * 4 + 0];// accum_count
    float sum_sky_visibility = ss_ray_sample_accum[gindex * 4 + 1];// sky_visibility_boolの合計.
    
    const float prev_reprojected_value = ss_temporal_reprojected_value[gindex];
    const float inv_hit_count = (hit_count > 0)? (1.0 / float(hit_count)) : 1.0;
    const float sky_visibility = (hit_count > 0)? (sum_sky_visibility * inv_hit_count) : prev_reprojected_value;

    float new_sky_visibility = sky_visibility;
    float reprojection_succeed = 0.0;
    if((0xffffffff != ss_temporal_best_prev_tile_packed) && (0 != cb_srvs.ss_probe_temporal_reprojection_enable))
    {
        float temporal_rate = biased_shadow_preserving_temporal_filter_weight(sky_visibility, prev_reprojected_value);
        temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);

        const float3 curr_camera_pos_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
        const float3 prev_camera_pos_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_prev_view_inv_mtx);

        new_sky_visibility = lerp(new_sky_visibility, prev_reprojected_value, temporal_rate);// 補間.
        reprojection_succeed = 1.0;
    }

    RWScreenSpaceProbeTex[global_pos] = float4(new_sky_visibility, prev_reprojected_value, ss_prev_radiance[gindex], reprojection_succeed);
}
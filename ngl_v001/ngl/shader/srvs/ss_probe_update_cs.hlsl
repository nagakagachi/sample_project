
#if 0

ss_probe_update_cs.hlsl

screen-reconstructed voxel structure
ScreenSpaceProbe更新.

#endif

#include "srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

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

// -------------------------------------

// https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf
// Algorithm 3: Biased shadow-preserving temporal hysteresis 
float biased_shadow_preserving_temporal_filter_weight(float curr_value, float prev_value, float min_clamp)
{
    const float l1 = curr_value;
    const float l2 = prev_value;
    float alpha = max(l1 - l2 - min(l1, l2), 0.0) / max(max(l1, l2), 1e-4);
    //alpha = CalcSquare(clamp(alpha, 0.0, 0.95));// オリジナル.
    alpha = clamp(alpha, min_clamp, 0.98);// 最低限前回の値を保持するために下限を追加.
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
    
    //const uint frame_rand = hash_uint32_iq(probe_id + cb_srvs.frame_count);


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
    }
    GroupMemoryBarrierWithGroupSync();
    
    // カメラ座標.
    const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
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
    const float3 ray_origin_base = ss_probe_pos_ws + ss_probe_approx_normal_ws * ray_origin_normal_offset;

    // レイ生成 + トレース結果をSharedに格納.
    const uint ray_count = NGL_SSP_RAY_COUNT;

    for(int sample_index = 0; sample_index < 1; ++sample_index)
    {
        const uint ray_index = gindex + ray_count * sample_index;

        float3 sample_ray_dir;
        #if 1
        {
            // Cos分布半球方向ランダム.
            const float3 unit_v3 = random_unit_vector3(float3(asfloat(global_pos.x), asfloat(global_pos.y), asfloat(ray_index^cb_srvs.frame_count)));

            const float3 local_dir = normalize(unit_v3 + float3(0.0, 0.0, 1.0));
            sample_ray_dir = local_dir.x * base_tangent_ws + local_dir.y * base_bitangent_ws + local_dir.z * base_normal_ws;
        }
        #elif 1
        {
            // MEMO. このフローの場合サンプル数増加でGPUハングが起きる? なにかの未定義動作を引いている? Normalizeで解消?.
            // 半球方向一様ランダム.
            float3 local_dir = random_unit_vector3(float3(asfloat(global_pos.x), asfloat(global_pos.y), asfloat(ray_index^cb_srvs.frame_count)));
            local_dir.z = abs(local_dir.z);
            sample_ray_dir = normalize(local_dir.x * base_tangent_ws + local_dir.y * base_bitangent_ws + local_dir.z * base_normal_ws);
        }
        #else
        {
            // OctMapセルに対応する方向にレイ発行. 半球方向と逆の場合は反転マッピング.
            const float2 noise_float2 = noise_float3_to_float2(float3(global_pos.xy, float(ray_index^cb_srvs.frame_count)));
            const float2 octmap_uv = (float2(probe_atlas_local_pos) + noise_float2) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
            sample_ray_dir = OctDecode(octmap_uv);
            // 常に法線方向に制限する.
            if(dot(sample_ray_dir, base_normal_ws) < 0.0)
            {
                sample_ray_dir = sample_ray_dir - 2.0 * dot(sample_ray_dir, base_normal_ws) * base_normal_ws;
            }
        }
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
        const int2 oct_cell_id = int2(oct_uv * SCREEN_SPACE_PROBE_TILE_SIZE);
        const int oct_cell_index = oct_cell_id.y * SCREEN_SPACE_PROBE_TILE_SIZE + oct_cell_id.x;
        
        // Result Accumulation.
        InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 0], 1);// accum_count
        InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 1], uint(sky_visibility));// sky_visibility_bool
    }
    GroupMemoryBarrierWithGroupSync();

    uint hit_count = ss_ray_sample_accum[gindex * 4 + 0];// accum_count
    float sum_sky_visibility = ss_ray_sample_accum[gindex * 4 + 1];// sky_visibility_boolの合計.
    
    const float prev_same_probe_value = 0.0;//ScreenSpaceProbeHistoryTex.Load(int3(global_pos, 0)).r;
    const float inv_hit_count = (hit_count > 0)? (1.0 / float(hit_count)) : 1.0;
    const float sky_visibility = (hit_count > 0)? (sum_sky_visibility * inv_hit_count) : prev_same_probe_value;


    float new_sky_visibility = sky_visibility;

    // Temporal Reprojection.
    float reprojection_succeed = 0.0;
    if(0 != cb_srvs.ss_probe_temporal_reprojection_enable)
    {
        if(0 == gindex)
        {
            ss_temporal_best_score = 0xffffffff;
            ss_temporal_best_prev_tile_packed = 0xffffffff;
        }
        ss_temporal_candidate_prev_tile_packed[gindex] = 0xffffffff;
        GroupMemoryBarrierWithGroupSync();

        uint local_best_score = 0xffffffff;
        uint local_best_prev_tile_packed = 0xffffffff;
        bool is_valid_prev_uv;
        const float2 prev_uv = CalcPrevFrameUvFromWorldPos(ss_probe_pos_ws, is_valid_prev_uv);
        if(is_valid_prev_uv)
        {
            const int2 full_res = int2(depth_size);
            const int tile_size = SCREEN_SPACE_PROBE_TILE_SIZE;
            const int2 probe_tile_count = max((full_res + tile_size - 1) / tile_size, int2(1, 1));
            const float2 prev_pos_texel = prev_uv * float2(full_res);
            const int2 prev_center_tile = clamp(int2(prev_pos_texel) / tile_size, int2(0, 0), probe_tile_count - 1);

            const int candidate_index = int(gindex);
            if(candidate_index < 9)
            {
                const int2 candidate_offset = int2(candidate_index % 3, candidate_index / 3) - int2(1, 1);
                const int2 candidate_tile_id = clamp(prev_center_tile + candidate_offset, int2(0, 0), probe_tile_count - 1);
                const float4 candidate_tile_info = ScreenSpaceProbeHistoryTileInfoTex.Load(int3(candidate_tile_id, 0));

                const bool is_candidate_depth_valid = isValidDepth(candidate_tile_info.x);
                const bool is_depth_matched = abs(candidate_tile_info.x - ss_probe_depth) <= cb_srvs.ss_probe_temporal_depth_threshold;
                const float3 candidate_normal_ws = OctDecode(candidate_tile_info.zw);
                const bool is_normal_matched = dot(candidate_normal_ws, ss_probe_approx_normal_ws) >= cb_srvs.ss_probe_temporal_normal_threshold_cos;

                if(is_candidate_depth_valid && is_depth_matched && is_normal_matched)
                {
                    const float2 candidate_center_texel = (float2(candidate_tile_id * tile_size) + float(tile_size) * 0.5);
                    const float2 candidate_delta = candidate_center_texel - prev_pos_texel;
                    const float candidate_dist2 = dot(candidate_delta, candidate_delta);
                    const uint quantized_dist = min((uint)(candidate_dist2 * 1024.0), 0x03ffffffu);
                    local_best_score = (quantized_dist << 6) | (gindex & 0x3fu);
                    local_best_prev_tile_packed = (uint(candidate_tile_id.y) << 16) | uint(candidate_tile_id.x & 0xffff);
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

        if(0xffffffff != ss_temporal_best_prev_tile_packed)
        {
            const int2 best_prev_tile = int2(int(ss_temporal_best_prev_tile_packed & 0xffffu), int((ss_temporal_best_prev_tile_packed >> 16) & 0xffffu));
            const int2 prev_global_pos = clamp(best_prev_tile * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos, int2(0, 0), int2(depth_size) - 1);
            const float4 prev_probe = ScreenSpaceProbeHistoryTex.Load(int3(prev_global_pos, 0));

            float temporal_rate = biased_shadow_preserving_temporal_filter_weight(sky_visibility, prev_probe.r, cb_srvs.ss_probe_temporal_min_hysteresis);
            temporal_rate = clamp(temporal_rate, cb_srvs.ss_probe_temporal_min_hysteresis, cb_srvs.ss_probe_temporal_max_hysteresis);

            const float3 curr_camera_pos_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
            const float3 prev_camera_pos_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_prev_view_inv_mtx);
            const float camera_motion = length(curr_camera_pos_ws - prev_camera_pos_ws);
            const float camera_motion_scale = saturate(1.0 - camera_motion * cb_srvs.ss_probe_temporal_camera_motion_scale);
            temporal_rate *= camera_motion_scale;

            new_sky_visibility = lerp(new_sky_visibility, prev_probe.r, temporal_rate);// 補間.
            reprojection_succeed = 1.0;
        }
    }

    RWScreenSpaceProbeTex[global_pos] = float4(new_sky_visibility, float(hit_count)*0.025, reprojection_succeed, 0.0);
}
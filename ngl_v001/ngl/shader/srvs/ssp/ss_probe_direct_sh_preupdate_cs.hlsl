#if 0

ss_probe_direct_sh_preupdate_cs.hlsl

DirectSH方式専用 ProbeタイルTileInfo 更新 + Temporal Reprojection Best Probe 探索.
既存の ss_probe_preupdate_cs.hlsl と同ロジックだが,
入出力先を DirectSH専用テクスチャ (ScreenSpaceProbeDirectSHTileInfoTex) に変更している.

追加処理:
  - Temporal Reprojection の Best Prev Tile 決定を行い,
    RWScreenSpaceProbeDirectSHBestPrevTileTex へ packed tile id を書き出す.
  - 1 ThreadGroup = 1 Tile とし, 25レーン協調で 5x5 探索を行う.

#endif

#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"
#include "../../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D                     TexHardwareDepth;

#define SEARCH_GROUP_SIZE_X SCREEN_SPACE_PROBE_DIRECT_SH_PREUPDATE_SEARCH_GROUP_SIZE
#define SEARCH_GROUP_SIZE_Y SCREEN_SPACE_PROBE_DIRECT_SH_PREUPDATE_SEARCH_GROUP_SIZE

#define PROBE_RELOCATION_RETRY_COUNT 8
// 0: disable, 1: depth-based neighbor normals
#define ENABLE_NEIGHBOR_NORMAL_FALLBACK 1

#define SEARCH_THREAD_COUNT (SEARCH_GROUP_SIZE_X * SEARCH_GROUP_SIZE_Y)
#define SEARCH_THREAD_INDEX_BITS 6u
#define SEARCH_THREAD_INDEX_MASK ((1u << SEARCH_THREAD_INDEX_BITS) - 1u)

#if SEARCH_THREAD_COUNT > (1 << SEARCH_THREAD_INDEX_BITS)
    #error "DirectSH preupdate search group size exceeds SEARCH_THREAD_INDEX_BITS capacity."
#endif

groupshared uint   gs_probe_valid;
groupshared float  gs_probe_depth;
groupshared uint2  gs_probe_pos_in_tile;
groupshared float2 gs_probe_normal_oct;
groupshared float3 gs_probe_pos_ws;
groupshared float3 gs_probe_normal_ws;
groupshared uint   gs_best_prev_tile_packed;
groupshared uint   gs_best_score;
groupshared uint   gs_candidate_prev_tile_packed[SEARCH_THREAD_COUNT];

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

bool TryEvaluateCandidateTile(
    int2 candidate_tile_id,
    float2 depth_size_inv,
    float3 current_probe_pos_ws,
    float3 current_probe_normal_ws,
    out uint quantized_dist)
{
    quantized_dist = 0xffffffffu;

    const float4 candidate_tile_info = ScreenSpaceProbeDirectSHHistoryTileInfoTex.Load(int3(candidate_tile_id, 0));
    if(!isValidDepth(candidate_tile_info.x))
        return false;

    const int2 candidate_probe_texel = candidate_tile_id * SCREEN_SPACE_PROBE_INFO_DOWNSCALE + SspTileInfoDecodeProbePosInTile(candidate_tile_info.y);
    const float candidate_view_z = calc_view_z_from_ndc_z(candidate_tile_info.x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    const float3 candidate_pos_vs = CalcViewSpacePosition((float2(candidate_probe_texel) + 0.5) * depth_size_inv, candidate_view_z, cb_ngl_sceneview.cb_prev_proj_mtx);
    const float3 candidate_pos_ws = mul(cb_ngl_sceneview.cb_prev_view_inv_mtx, float4(candidate_pos_vs, 1.0));
    const float3 diff_ws = candidate_pos_ws - current_probe_pos_ws;
    const float plane_dist = abs(dot(diff_ws, current_probe_normal_ws));
    const float normal_dot = dot(current_probe_normal_ws, OctDecode(candidate_tile_info.zw));

    if(plane_dist >= cb_srvs.ss_probe_temporal_filter_plane_dist_threshold
        || normal_dot <= cb_srvs.ss_probe_temporal_filter_normal_cos_threshold)
    {
        return false;
    }

    float probe_dist = length(diff_ws) * 100.0;
    probe_dist += (1.0 - normal_dot) * 1.0;
    quantized_dist = min((uint)(probe_dist * 1024.0), 0x03ffffffu);
    return true;
}

[numthreads(SEARCH_GROUP_SIZE_X, SEARCH_GROUP_SIZE_Y, 1)]
void main_cs(
    uint3 gtid   : SV_GroupThreadID,
    uint  gindex : SV_GroupIndex,
    uint3 gid    : SV_GroupID
)
{
    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);

    uint2 tile_info_size;
    ScreenSpaceProbeDirectSHHistoryTileInfoTex.GetDimensions(tile_info_size.x, tile_info_size.y);
    if(any(gid.xy >= tile_info_size))
        return;

    const int2 probe_id = gid.xy;
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_INFO_DOWNSCALE;

    gs_candidate_prev_tile_packed[gindex] = 0xffffffffu;

    if(0 == gindex)
    {
        RandomInstance rng;
        rng.rngState = asuint(noise_float_to_float(float3(probe_id.x, probe_id.y, cb_srvs.frame_count)));

        uint2 probe_pos_in_tile = uint2(0, 0);
        int2 current_probe_texel_pos = ss_probe_tile_pixel_start;
        float probe_depth = 1.0;
        {
            const float4 prev_info = ScreenSpaceProbeDirectSHHistoryTileInfoTex[probe_id];

            probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(prev_info.y);
            current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
            probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;

            const float relocation_probability = cb_srvs.ss_probe_preupdate_relocation_probability;
            if(!isValidDepth(probe_depth) || (relocation_probability > rng.rand()))
            {
                const uint2 probe_pos_limit = uint2(SCREEN_SPACE_PROBE_INFO_DOWNSCALE, SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
                for(int i = 0; i < PROBE_RELOCATION_RETRY_COUNT; ++i)
                {
                    probe_pos_in_tile = min(uint2(rng.rand2() * probe_pos_limit), probe_pos_limit - 1);
                    current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
                    probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
                    if(isValidDepth(probe_depth))
                        break;
                }
            }
        }

        gs_probe_valid = 0;
        gs_probe_depth = probe_depth;
        gs_probe_pos_in_tile = probe_pos_in_tile;
        gs_probe_normal_oct = float2(0.0, 0.0);
        gs_probe_pos_ws = float3(0.0, 0.0, 0.0);
        gs_probe_normal_ws = float3(0.0, 0.0, 1.0);
        gs_best_prev_tile_packed = 0xffffffffu;
        gs_best_score = 0xffffffffu;

        if(isValidDepth(probe_depth))
        {
            const float2 probe_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;
            const float3 pixel_pos_vs = CalcViewSpacePosition(probe_uv, calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

            const float3 approx_normal_vs = reconstruct_normal_vs_fine(TexHardwareDepth, current_probe_texel_pos, probe_depth, depth_size_inv, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef, cb_ngl_sceneview.cb_proj_mtx);
            const float3 approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);
            float3 final_normal_ws = normalize(approx_normal_ws);

            #if ENABLE_NEIGHBOR_NORMAL_FALLBACK == 1
            {
                const float k_normal_cos_threshold = 0.6;
                float3 neighbor_normal_sum = float3(0.0, 0.0, 0.0);
                uint neighbor_count = 0;
                const int2 neighbor_offsets[4] = {
                    int2(-1, 0),
                    int2(1, 0),
                    int2(0, -1),
                    int2(0, 1)
                };

                for(int i = 0; i < 4; ++i)
                {
                    const int2 neighbor_tile_id = ss_probe_tile_id + neighbor_offsets[i];
                    if(neighbor_tile_id.x < 0 || neighbor_tile_id.y < 0 || neighbor_tile_id.x >= (int)tile_info_size.x || neighbor_tile_id.y >= (int)tile_info_size.y)
                        continue;

                    const float4 neighbor_info = ScreenSpaceProbeDirectSHHistoryTileInfoTex[neighbor_tile_id];
                    const int2 neighbor_probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(neighbor_info.y);
                    const int2 neighbor_tile_pixel_start = neighbor_tile_id * SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
                    const int2 neighbor_probe_texel_pos = neighbor_tile_pixel_start + neighbor_probe_pos_in_tile;
                    const float neighbor_depth = TexHardwareDepth.Load(int3(neighbor_probe_texel_pos, 0)).r;
                    if(!isValidDepth(neighbor_depth))
                        continue;

                    const float3 neighbor_normal_vs = reconstruct_normal_vs_fine(TexHardwareDepth, neighbor_probe_texel_pos, neighbor_depth, depth_size_inv, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef, cb_ngl_sceneview.cb_proj_mtx);
                    const float3 neighbor_normal_ws = normalize(mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, neighbor_normal_vs));

                    neighbor_normal_sum += neighbor_normal_ws;
                    neighbor_count += 1;
                }

                if(neighbor_count > 0)
                {
                    const float3 neighbor_avg_ws = normalize(neighbor_normal_sum);
                    if(dot(final_normal_ws, neighbor_avg_ws) < k_normal_cos_threshold)
                    {
                        final_normal_ws = neighbor_avg_ws;
                    }
                }
            }
            #endif

            gs_probe_valid = 1;
            gs_probe_normal_oct = OctEncode(final_normal_ws);
            gs_probe_pos_ws = pixel_pos_ws;
            gs_probe_normal_ws = final_normal_ws;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if(0 != gs_probe_valid && 0 != cb_srvs.ss_probe_temporal_reprojection_enable)
    {
        bool is_valid_prev_uv = false;
        const float2 prev_uv = CalcPrevFrameUvFromWorldPos(gs_probe_pos_ws, is_valid_prev_uv);
        if(is_valid_prev_uv)
        {
            const int tile_size = SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
            const int2 probe_tile_count = int2(tile_info_size);
            const float2 prev_pos_texel = prev_uv * float2(depth_size);
            const int2 prev_center_tile = clamp(int2(prev_pos_texel) / tile_size, int2(0, 0), probe_tile_count - 1);

            const int2 search_origin_tile = prev_center_tile - int2(SEARCH_GROUP_SIZE_X / 2, SEARCH_GROUP_SIZE_Y / 2);
            const int2 candidate_tile_id = clamp(search_origin_tile + int2(gtid.xy), int2(0, 0), probe_tile_count - 1);

            uint quantized_dist;
            if(TryEvaluateCandidateTile(candidate_tile_id, depth_size_inv, gs_probe_pos_ws, gs_probe_normal_ws, quantized_dist))
            {
                const uint local_best_score = (quantized_dist << SEARCH_THREAD_INDEX_BITS) | (gindex & SEARCH_THREAD_INDEX_MASK);
                gs_candidate_prev_tile_packed[gindex] = SspPackTileId(candidate_tile_id);
                InterlockedMin(gs_best_score, local_best_score);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if(0 == gindex && 0xffffffffu != gs_best_score)
    {
        const uint winner_lane = gs_best_score & SEARCH_THREAD_INDEX_MASK;
        gs_best_prev_tile_packed = gs_candidate_prev_tile_packed[winner_lane];
    }
    GroupMemoryBarrierWithGroupSync();

    if(0 != gindex)
        return;

    if(0 != gs_probe_valid)
    {
        const bool is_reprojection_succeeded = (0xffffffffu != gs_best_prev_tile_packed);
        RWScreenSpaceProbeDirectSHTileInfoTex[probe_id] = SspTileInfoBuild(gs_probe_depth, gs_probe_pos_in_tile, gs_probe_normal_oct, is_reprojection_succeeded);
        RWScreenSpaceProbeDirectSHBestPrevTileTex[probe_id] = gs_best_prev_tile_packed;
    }
    else
    {
        RWScreenSpaceProbeDirectSHTileInfoTex[probe_id] = SspTileInfoBuild(1.0, uint2(0, 0), float2(0.0, 0.0), false);
        RWScreenSpaceProbeDirectSHBestPrevTileTex[probe_id] = 0xffffffffu;
    }
}

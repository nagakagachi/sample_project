
#if 0

ss_probe_update_cs.hlsl

ScreenSpaceProbe更新.

#endif

#include "ssvg_util.hlsli"
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

// Ray temporary storage per tile.
groupshared float4 ss_ray_sample_cache[NGL_SSP_RAY_COUNT];

groupshared uint ss_ray_sample_accum[NGL_SSP_RAY_COUNT * 4];// accum_count, sky_visibility_bool,,

// -------------------------------------

bool isValidDepth(float d)
{
    // 
    return (0.0 < d && d < 1.0);
}

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


[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
    uint gindex : SV_GroupIndex,
	uint3 gid : SV_GroupID
)
{
    const int2 frame_skip_probe_offset = 
    int2(cb_ssvg.frame_count % cb_ssvg.ss_probe_temporal_update_group_size, (cb_ssvg.frame_count / cb_ssvg.ss_probe_temporal_update_group_size) % cb_ssvg.ss_probe_temporal_update_group_size);

    const int2 probe_atlas_local_pos = gtid.xy;// タイル内でのローカル位置は時間分散でスキップされないのでそのまま.
    const int2 probe_id = gid.xy * cb_ssvg.ss_probe_temporal_update_group_size + frame_skip_probe_offset;// プローブフレームスキップ考慮.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;// グローバルテクセル位置計算.
    
    const uint frame_rand = hash_uint32_iq(probe_id + cb_ssvg.frame_count);


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
        ss_ray_sample_cache[gindex] = float4(0.0, 0.0, 0.0, 0.0);

        ss_ray_sample_accum[gindex * 4 + 0] = 0;// accum_count
        ss_ray_sample_accum[gindex * 4 + 1] = 0;// sky_visibility_bool
        ss_ray_sample_accum[gindex * 4 + 2] = 0;// unused
        ss_ray_sample_accum[gindex * 4 + 3] = 0;// unused
    }
    GroupMemoryBarrierWithGroupSync();
    
    // カメラ座標.
    const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 base_normal_ws = ss_probe_approx_normal_ws;

    // レイ方向オフセット. sqrt(3.0).
    const float ray_start_offset_scale = cb_ssvg.ss_probe_ray_start_offset_scale;
    const float ray_origin_start_offset = cb_ssvg.bbv.cell_size * k_bbv_per_voxel_resolution_inv * ray_start_offset_scale;
    // 近似法線方向オフセット.
    const float ray_origin_normal_offset_scale = cb_ssvg.ss_probe_ray_normal_offset_scale;
    const float ray_origin_normal_offset = cb_ssvg.bbv.cell_size * k_bbv_per_voxel_resolution_inv * ray_origin_normal_offset_scale;
    const float3 ray_origin_base = ss_probe_pos_ws + ss_probe_approx_normal_ws * ray_origin_normal_offset;

    // レイ生成 + トレース結果をSharedに格納.
    const uint ray_index = gindex;
    const uint ray_count = NGL_SSP_RAY_COUNT;
    if(ray_index < ray_count)
    {
        #if 1
            // OctMapセルの方向にレイ発行. 半球方向と逆の場合は反転マッピング.
            const float2 noise_float2 = noise_float3_to_float2(float3(global_pos.xy, float(frame_rand))) * 2.0 - 1.0;
            const float2 octmap_uv = (float2(probe_atlas_local_pos) + 0.5 + noise_float2 * 0.5) * SCREEN_SPACE_PROBE_TILE_SIZE_INV;
            const float3 sample_ray_dir_local = SspDecodeRayDirLocal(octmap_uv);
            
            float3 sample_ray_dir = SspBuildSampleRayDirFromLocal(sample_ray_dir_local, base_normal_ws);
            //　常に法線方向に制限する.
            if(dot(sample_ray_dir, base_normal_ws) < 0.0)
            {
                sample_ray_dir = reflect(sample_ray_dir, base_normal_ws);
            }
        #else
            // 半球方向ランダム検証 RayGuidingしたい.
            const float2 rand01 = noise_float3_to_float2(float3(global_pos.xy, float(frame_rand + ray_index * 131u)));
            const float cos_theta = rand01.x;
            const float sin_theta = sqrt(saturate(1.0 - cos_theta * cos_theta));
            const float phi = rand01.y * NGL_2PI;
            const float3 local_dir = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

            const float3 sample_ray_dir = SspBuildSampleRayDirFromLocal(local_dir, base_normal_ws);
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
            cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
            cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

        const float sky_visibility = (0.0 > curr_ray_t_ws.x)? 1.0 : 0.0;// 負ならヒットなしで空が見えている.

        const float2 oct_uv = SspEncodeDirByNormal(sample_ray_dir, base_normal_ws);// レイ方向を法線基準のOctahedralマップUVにエンコードして格納.
        const int2 oct_cell_id = int2(oct_uv * SCREEN_SPACE_PROBE_TILE_SIZE);
        const int oct_cell_index = oct_cell_id.y * SCREEN_SPACE_PROBE_TILE_SIZE + oct_cell_id.x;

        // スレッド毎に結果を保存.
        ss_ray_sample_cache[ray_index] = float4(1.0, sky_visibility, oct_cell_index, 0.0);

        
            InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 0], 1);// accum_count
            InterlockedAdd(ss_ray_sample_accum[oct_cell_index * 4 + 1], uint(sky_visibility));// sky_visibility_bool
    }
    GroupMemoryBarrierWithGroupSync();

    uint hit_count = ss_ray_sample_accum[gindex * 4 + 0];// accum_count
    float sum_sky_visibility = ss_ray_sample_accum[gindex * 4 + 1];// sky_visibility_boolの合計.
    
    const float4 prev_probe = RWScreenSpaceProbeTex[global_pos];
    const float inv_hit_count = (hit_count > 0)? (1.0 / float(hit_count)) : 1.0;
    const float sky_visibility = (hit_count > 0)? (sum_sky_visibility * inv_hit_count) : prev_probe.r;

    const float temporal_rate = biased_shadow_preserving_temporal_filter_weight(sky_visibility, prev_probe.r, 0.89);
    const float4 hit_debug = float4(sky_visibility, 0.0, 0.0, temporal_rate);

    RWScreenSpaceProbeTex[global_pos] = lerp( hit_debug, prev_probe, temporal_rate);
}
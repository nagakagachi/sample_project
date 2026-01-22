
#if 0

ss_probe_clear_cs.hlsl

ScreenSpaceProbe用テクスチャクリア.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			           TexHardwareDepth;

#define SCREEN_SPACE_PROBE_TILE_SIZE 8

groupshared float tile_hw_depth;
groupshared float tile_view_z;
groupshared float3 tile_pos_vs;
groupshared float3 tile_pos_ws;

[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID
)
{
    const int2 frame_skip_probe_offset = 
    int2(cb_ssvg.frame_count % cb_ssvg.screen_probe_temporal_update_tile_size, (cb_ssvg.frame_count / cb_ssvg.screen_probe_temporal_update_tile_size) % cb_ssvg.screen_probe_temporal_update_tile_size);

    const int2 probe_atlas_local_pos = gtid.xy;// タイル内でのローカル位置は時間分散でスキップされないのでそのまま.
    const int2 probe_id = gid.xy * cb_ssvg.screen_probe_temporal_update_tile_size + frame_skip_probe_offset;// プローブフレームスキップ考慮.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;// グローバルテクセル位置計算.
    

    //cb_ssvg.frame_count;
    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    
    const float2 screen_uv = (float2(global_pos) + float2(0.5, 0.5)) / float2(depth_size);// ピクセル中心への半ピクセルオフセット考慮.

    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    const uint frame_rand = noise_iqint32_orig(probe_id + cb_ssvg.frame_count);
    const uint2 rand_element_in_tile = uint2(frame_rand / SCREEN_SPACE_PROBE_TILE_SIZE, frame_rand) % SCREEN_SPACE_PROBE_TILE_SIZE;

    const int2 rand_texel_pos = ss_probe_tile_pixel_start + rand_element_in_tile;
    const float2 rand_texel_uv = (float2(rand_texel_pos) + float2(0.5, 0.5)) / float2(depth_size);// ピクセル中心への半ピクセルオフセット考慮.
    
    if(all(probe_atlas_local_pos == 0))
    {
        // クリア
        tile_hw_depth = 1.0;
        tile_view_z = 0.0;
        tile_pos_vs = float3(0.0, 0.0, 0.0);
        tile_pos_ws = float3(0.0, 0.0, 0.0);

        // プローブのレイ始点とするピクセルの深度取得.
        const float d = TexHardwareDepth.Load(int3(rand_texel_pos, 0)).r;
        // 空ピクセルだった場合は再トライするほうが良いか？検討.
        if(0.0 < d && d < 1.0)
        {
            const float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pixel_pos_vs = CalcViewSpacePosition(rand_texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

            // 今回タイルが処理するプローブ候補の情報.
            tile_hw_depth = d;
            tile_view_z = view_z;
            tile_pos_vs = pixel_pos_vs;
            tile_pos_ws = pixel_pos_ws;
        }
    }
    GroupMemoryBarrierWithGroupSync();// タイル代表情報の共有メモリ書き込み待ち.


    // この後さらに共有メモリでバリアする場合などはリターンできなくなるかも.
    if(1.0 <= tile_hw_depth)
    {
        RWScreenSpaceProbeTex[global_pos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // カメラ座標.
    const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    #if 0
        // 担当アトラステクセルのOctahedral方向の固定レイ方向.
        const float3 sample_ray_dir = OctDecode(float2(probe_atlas_local_pos + 0.5)/SCREEN_SPACE_PROBE_TILE_SIZE);
    #else
        // 担当アトラステクセルのOctahedral方向にノイズを付加したレイ方向.
        const float2 noise_float2 = float2(
            noise_iqint32(float2(global_pos.x, global_pos.y ^ frame_rand)),
            noise_iqint32(float2(global_pos.y ^ frame_rand, global_pos.x))
        ) * 2.0 - 1.0;
        const float3 sample_ray_dir = OctDecode(((float2(probe_atlas_local_pos) + 0.5 + noise_float2*0.5)/float(SCREEN_SPACE_PROBE_TILE_SIZE)));
    #endif

    const float3 sample_ray_origin = tile_pos_ws + (normalize(view_origin - tile_pos_ws) * 1.1);// 少しカメラ寄りからレイを飛ばす.
    // Voxel単位Traceのテスト.
    const float trace_distance = 50.0;          
    int hit_voxel_index = -1;
    float4 debug_ray_info;
    float4 curr_ray_t_ws = trace_bbv_dev(
        hit_voxel_index, debug_ray_info,
        sample_ray_origin, sample_ray_dir, trace_distance, 
        cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
        cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

    // ヒットしなかったら空が見えているものとしてその方向を格納.
    const float3 hit_debug = (0.0 > curr_ray_t_ws.x)? sample_ray_dir : 0.0;

    // 仮書き込み.
    RWScreenSpaceProbeTex[global_pos] = lerp( RWScreenSpaceProbeTex[global_pos], float4(hit_debug, (0.0 > curr_ray_t_ws.x)? 1.0 : 0.0), 0.1);
}
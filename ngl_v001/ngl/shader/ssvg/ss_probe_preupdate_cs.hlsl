
#if 0

ss_probe_preupdate_cs.hlsl

ScreenSpaceProbe ProbeTile用の情報更新.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			           TexHardwareDepth;

// -------------------------------------

bool isValidDepth(float d)
{
    // 
    return (0.0 < d && d < 1.0);
}

#define DISPATCH_GROUP_SIZE_X 8
#define DISPATCH_GROUP_SIZE_Y 8

[numthreads(DISPATCH_GROUP_SIZE_X, DISPATCH_GROUP_SIZE_Y, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID
)
{
    const int2 probe_id = dtid.xy;// フル解像度に対して 1/8 で, ScreenSpaceProbeごとに1テクセル.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE;
    
	const float3 camera_pos = cb_ngl_sceneview.cb_view_inv_mtx._m03_m13_m23;

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);
    
    const float2 screen_uv = (float2(global_pos) + float2(0.5, 0.5)) * depth_size_inv;// ピクセル中心への半ピクセルオフセット考慮.

    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    const uint frame_rand = hash_uint32_iq(probe_id + (probe_id ^ cb_ssvg.frame_count));
    const uint2 rand_element_in_tile = uint2(frame_rand * SCREEN_SPACE_PROBE_TILE_SIZE_INV, frame_rand) % SCREEN_SPACE_PROBE_TILE_SIZE;

    // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
    const int2 current_probe_texel_pos = ss_probe_tile_pixel_start + rand_element_in_tile;
    const float2 current_probe_texel_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;// ピクセル中心への半ピクセルオフセット考慮.
    

    // プローブの配置テクセルの深度取得.
    const float d = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
    // Skyのテクセルだった場合は再トライするほうが良いか？検討.


    if(isValidDepth(d))
    {
        // 自己遮蔽回避用のサーフェイス探索レイトレース.
        const float3 pixel_pos_vs = CalcViewSpacePosition(screen_uv, calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));
        
        const float3 sample_ray_origin = pixel_pos_ws;
        const float3 sample_ray_vec = camera_pos - sample_ray_origin;
        const float3 sample_ray_dir = normalize(sample_ray_vec);

        const float trace_distance = 0.5;
        int hit_voxel_index = -1;
        float4 debug_ray_info;
        float4 curr_ray_t_ws = 
        trace_bbv_inverse_bit
        (
            hit_voxel_index, debug_ray_info,
            sample_ray_origin, sample_ray_dir, trace_distance, 
            cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
            cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

        // Bbvでヒットしなかった場合はサーフェイスまでの距離をそのまま格納.
        const float surface_hit_distance = (curr_ray_t_ws.x > 0.0)? curr_ray_t_ws.x : trace_distance;
        // 配置できたらその情報を格納.
        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(d, rand_element_in_tile.x, rand_element_in_tile.y, surface_hit_distance);
    }
    else
    {
        // 配置失敗の場合はBbvレイトレース距離を負数.
        RWScreenSpaceProbeTileInfoTex[probe_id].w = -1.0;
    }

}
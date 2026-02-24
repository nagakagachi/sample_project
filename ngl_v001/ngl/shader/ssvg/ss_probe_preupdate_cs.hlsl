
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

#define DISPATCH_GROUP_SIZE_X SCREEN_SPACE_PROBE_TILE_SIZE
#define DISPATCH_GROUP_SIZE_Y SCREEN_SPACE_PROBE_TILE_SIZE

[numthreads(DISPATCH_GROUP_SIZE_X, DISPATCH_GROUP_SIZE_Y, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID
)
{
    const int2 probe_id = dtid.xy;// フル解像度に対して 1/SCREEN_SPACE_PROBE_TILE_SIZE で, ScreenSpaceProbeごとに1テクセル.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE;
    
	const float3 camera_pos = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);
    
    const float2 screen_uv = (float2(global_pos) + float2(0.5, 0.5)) * depth_size_inv;// ピクセル中心への半ピクセルオフセット考慮.

    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    // Tile内探索.
    uint2 probe_pos_in_tile = uint2(0,0);
    float probe_depth = 1.0;
    #if 1
        // 前回情報
        const float4 prev_probe_info = RWScreenSpaceProbeTileInfoTex[probe_id];

        uint frame_rand = uint(prev_probe_info.y);
        // 前回が失敗だった場合, または一定の確率でランダム選択.
        if(!isValidDepth(prev_probe_info.x) || 0.1 > noise_float_to_float(asfloat(cb_ssvg.frame_count ^ probe_id.x + probe_id.y)))
        {
            frame_rand = hash_uint32_iq(probe_id + (cb_ssvg.frame_count ^ probe_id));
        }

        // 何回かリトライする.
        for(int i = 0; i < SCREEN_SPACE_PROBE_TILE_SIZE; ++i)
        {
            probe_pos_in_tile = uint2(frame_rand * SCREEN_SPACE_PROBE_TILE_SIZE_INV, frame_rand) % SCREEN_SPACE_PROBE_TILE_SIZE;
            // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
            const int2 current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
            // プローブの配置テクセルの深度取得.
            probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
            if(isValidDepth(probe_depth))
                break;

            // 次のセルを試行.
            frame_rand = hash_uint32_iq(probe_id * frame_rand + (cb_ssvg.frame_count ^ probe_id));
        }
    #else
        const uint frame_rand = hash_uint32_iq(probe_id + (probe_id ^ cb_ssvg.frame_count));
        probe_pos_in_tile = uint2(frame_rand * SCREEN_SPACE_PROBE_TILE_SIZE_INV, frame_rand) % SCREEN_SPACE_PROBE_TILE_SIZE;
        // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
        const int2 current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
        // プローブの配置テクセルの深度取得.
        probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
    #endif

    if(isValidDepth(probe_depth))
    {
        const float3 pixel_pos_vs = CalcViewSpacePosition(screen_uv, calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));
        
        // タイル内のプローブ位置をフラットインデックス化.
        const int probe_pos_flat_index_in_tile = probe_pos_in_tile.y * SCREEN_SPACE_PROBE_TILE_SIZE + probe_pos_in_tile.x;
        // 配置できたらその情報を格納.
        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(probe_depth, probe_pos_flat_index_in_tile, 0.0, 0.0);
    }
    else
    {
        RWScreenSpaceProbeTileInfoTex[probe_id].x = 1.0;
    }

}
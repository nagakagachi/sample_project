
#if 0

ss_probe_preupdate_cs.hlsl

ScreenSpaceProbe ProbeTile用の情報更新.

#endif

#include "srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"
#include "../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			           TexHardwareDepth;

// -------------------------------------

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
    // RandomInstance.
    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(probe_id.x, probe_id.y, cb_srvs.frame_count)));
    
    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);
    
    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    // Tile内探索.
    uint2 probe_pos_in_tile = uint2(0,0);
    int2 current_probe_texel_pos = ss_probe_tile_pixel_start;
    float probe_depth = 1.0;
    {
        // 再配置を確率的に実行する.
        const float4 prev_info = RWScreenSpaceProbeTileInfoTex[probe_id];
        probe_pos_in_tile = int2(prev_info.g % SCREEN_SPACE_PROBE_TILE_SIZE, prev_info.g / SCREEN_SPACE_PROBE_TILE_SIZE);// 前回のプローブ位置をタイル内で復元.
        current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;// 前回のプローブ位置をフル解像度テクセル位置に変換.
        probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;// 前回のプローブ位置の深度を取得.

        const bool force_relocation = false;//(ss_probe_tile_pixel_start.x < (1920/2));
        const float relocation_probability = 0.05;// 有効なプローブ位置が見つかっても一定確率で再配置する.
        if(force_relocation || (!isValidDepth(probe_depth) || relocation_probability >= rng.rand()))
        {
            // 何回かリトライする.
            for(int i = 0; i < SCREEN_SPACE_PROBE_TILE_SIZE; ++i)
            {
                probe_pos_in_tile = rng.rand2() * (SCREEN_SPACE_PROBE_TILE_SIZE - 1);
                // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
                current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
                // プローブの配置テクセルの深度取得.
                probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
                if(isValidDepth(probe_depth))
                    break;// 有効な深度であれば発見終了.
            }
        }
    }

    // 有効なプローブ位置が決定できた.
    if(isValidDepth(probe_depth))
    {
        const float2 probe_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;
        const float3 pixel_pos_vs = CalcViewSpacePosition(probe_uv, calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

        const float3 approx_normal_vs = reconstruct_normal_vs_fine(TexHardwareDepth, current_probe_texel_pos, probe_depth, depth_size_inv, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef, cb_ngl_sceneview.cb_proj_mtx);
        const float3 approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);
        
        // タイル内のプローブ位置をフラットインデックス化.
        const int probe_pos_flat_index_in_tile = probe_pos_in_tile.y * SCREEN_SPACE_PROBE_TILE_SIZE + probe_pos_in_tile.x;
        // 配置できたらその情報を格納.
        const float2 approx_normal_oct = OctEncode(normalize(approx_normal_ws));


        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(probe_depth, probe_pos_flat_index_in_tile, approx_normal_oct.x, approx_normal_oct.y);
    }
    else
    {
        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(1.0, 0, 0, 0);// 配置できなかった場合はDepthに負の値を入れておくなどして無効化.
    }

}
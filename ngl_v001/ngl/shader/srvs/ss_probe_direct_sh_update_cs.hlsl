
#if 0

ss_probe_direct_sh_update_cs.hlsl

ScreenSpaceProbe DirectSH 更新.
プリアップデートパスで事前計算された BestPrevTile を読み取り、セル毎の SH ヒストリ展開を行う.
インターロック不要 (3x3 探索はプリアップデートパスで完結している).

#endif

#include "srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;


// -------------------------------------
// Tile ThreadGroup共有のため共有メモリ.

// TileのこのフレームでのScreenSpaceProbe配置テクセル情報.
groupshared float ss_probe_hw_depth;
groupshared float3 ss_probe_pos_ws;
// ScreenSpaceProbe配置位置の近似法線情報.
groupshared float3 ss_probe_approx_normal_ws;

// プリアップデートで決定した DirectSH 再投影用の前フレームタイル ID.
groupshared uint gs_best_prev_tile_packed;

// -------------------------------------


[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
    uint gindex : SV_GroupIndex,
	uint3 gid : SV_GroupID
)
{
    const int2 probe_atlas_local_pos = gtid.xy;// タイル内でのローカル位置.
    const int2 probe_id = gid.xy;// フレームスキップなし: 全タイルを処理.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;// グローバルテクセル位置.

    // タイルのプローブ配置情報を代表スレッドが取得して共有メモリへ書き込む.
    if(all(probe_atlas_local_pos == 0))
    {
        // クリア.
        ss_probe_hw_depth = 1.0;
        ss_probe_pos_ws = float3(0.0, 0.0, 0.0);
        ss_probe_approx_normal_ws = float3(0.0, 0.0, 0.0);

        // プリアップデートで計算した BestPrevTile を読み取る.
        gs_best_prev_tile_packed = ScreenSpaceProbeDirectSHBestPrevTileTex.Load(int3(probe_id, 0));

        // ScreenSpaceProbeTileInfoTex からこのフレームのタイル情報を取得.
        const float4 tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(probe_id, 0));
        const float probe_depth = tile_info.x;
        if(isValidDepth(probe_depth))
        {
            const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
            const int2 ss_probe_tile_pixel_start = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE;
            const int2 probe_pos_rand_in_tile = int2(int(tile_info.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(tile_info.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
            const int2 probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_rand_in_tile;
            const float2 probe_uv = (float2(probe_texel_pos) + 0.5) * depth_size_inv;
            const float view_z = calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pos_vs = CalcViewSpacePosition(probe_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pos_vs, 1.0));
            const float3 approx_normal_ws = OctDecode(tile_info.zw);

            ss_probe_hw_depth = probe_depth;
            ss_probe_pos_ws = pos_ws;
            ss_probe_approx_normal_ws = approx_normal_ws;
        }
    }
    GroupMemoryBarrierWithGroupSync();// タイル代表情報の共有メモリ書き込み待ち.

    // タイルが有効なプローブを持たない場合は終了.
    if(!isValidDepth(ss_probe_hw_depth))
        return;

    // プリアップデートパスで事前計算された BestPrevTile を利用した DirectSH ヒストリ展開.
    // InterlockedMin 等の同期コストなしに再投影結果を参照できる.
    const bool has_valid_reprojection = (gs_best_prev_tile_packed != 0xffffffffu);
    if(has_valid_reprojection)
    {
        const uint2 best_prev_tile_id = SspUnpackTileId(gs_best_prev_tile_packed);
        const int2 prev_cell_global_pos = int2(best_prev_tile_id) * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;

        // ヒストリ展開: 前フレームの同一セル位置から DirectSH データをブレンド.
        // (実際の SH データテクスチャが追加された際にここで読み取り・ブレンド処理を行う.)
        // const float4 prev_sh = DirectSHHistoryTex[prev_cell_global_pos];
        // ...ブレンド処理...
    }
}

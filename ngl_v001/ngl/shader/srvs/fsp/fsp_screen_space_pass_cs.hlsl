
#if 0

fsp_screen_space_pass_cs.hlsl

ハードウェア深度バッファをもとにFrustumSurfaceProbeの処理をする.
可視サーフェイス上にあるFsp要素リストの生成.

#endif

#define TILE_WIDTH 16

#include "../srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			TexHardwareDepth;


// ThreadGroupタイル単位でスキップする最適化のグループタイル幅. 1より大きい数値で実行.
#define THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH 4

// 同一フレーム内の重複を atomic_work で潰しながら visible cell list へ積む。
void FspRegisterVisibleCell(uint global_cell_index)
{
    // Visible判定フレーム番号を書き込み.
    uint old_atomic_work = 0;
    InterlockedExchange(RWFspProbeBuffer[global_cell_index].atomic_work, cb_srvs.frame_count, old_atomic_work);

    // 交換前の値でVisible判定フレーム番号が現在フレームと異なるならリストへ登録. 別スレッドで同じVoxelを処理している場合の重複を除去する.
    if(cb_srvs.frame_count != old_atomic_work)
    {
        int current_visible_count = 0;
        InterlockedAdd(RWSurfaceProbeCellList[0], 1, current_visible_count);
        if(cb_srvs.fsp_visible_voxel_buffer_size > current_visible_count)
        {
            // 追加可能であれば登録. 登録位置はindex0のカウンタを除いた位置.
            RWSurfaceProbeCellList[current_visible_count + 1] = global_cell_index;
        }
        else
        {
            // サイズオーバーの場合はカウンタを戻す.
            InterlockedAdd(RWSurfaceProbeCellList[0], -1);
        }
    }
}

// DepthBufferに対してDispatch.
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_srvs.tex_main_view_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

    #if 1 < THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH
        // 適当なTile単位処理スキップ軽量化.
        const uint skip_tile_size = THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH;// SxS個のタイルグループ毎に1Fに1タイルだけ処理するシンプル軽量化.
        const uint tile_skip_id_x = gid.x%skip_tile_size;
        const uint tile_skip_id_y = gid.y%skip_tile_size;

        const uint skip_frame_id = cb_srvs.frame_count % (skip_tile_size*skip_tile_size);
        const uint skip_frame_id_y = skip_frame_id / (skip_tile_size);
        const uint skip_frame_id_x = skip_frame_id % (skip_tile_size);

        if((tile_skip_id_x != skip_frame_id_x) || (tile_skip_id_y != skip_frame_id_y))
        {
            return;
        }
    #endif


    // Tile単位で処理やAtomic書き出しをまとめることで効率化可能なはず.

    float d = TexHardwareDepth.Load(int3(dtid.xy, 0)).r;
    float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);

    // Skyチェック.
    if(65535.0 <= abs(view_z))
        return;

    // 深度->PixelWorldPosition
    const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, cb_ngl_sceneview.cb_proj_mtx);
    const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));

    const float3 to_surface_vec_ws = pixel_pos_ws - view_origin;
    const float to_surface_len_sq = dot(to_surface_vec_ws, to_surface_vec_ws);
    const bool has_surface_dir = (to_surface_len_sq > 1e-6);
    const float3 surface_view_dir_ws = has_surface_dir ? (to_surface_vec_ws * rsqrt(to_surface_len_sq)) : 0.0.xxx;

    const uint cascade_count = FspCascadeCount();
    [unroll]
    for(uint cascade_index = 0; cascade_index < k_fsp_max_cascade_count; ++cascade_index)
    {
        if(cascade_index >= cascade_count)
        {
            break;
        }

        const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
        const float half_cell_size = cascade.grid.cell_size * 0.5;
        // 基準セルは depth 位置そのものではなく、表面からカメラ側へ半セルだけ寄せた位置で選ぶ。
        const float3 base_probe_pos_ws = has_surface_dir ? (pixel_pos_ws - surface_view_dir_ws * half_cell_size) : pixel_pos_ws;

        uint global_cell_index = 0;
        if(!FspTryGetGlobalCellIndexFromWorldPos(base_probe_pos_ws, cascade_index, global_cell_index))
        {
            continue;
        }

        FspRegisterVisibleCell(global_cell_index);

        if((0 != cb_srvs.fsp_spawn_far_cell_enable) && has_surface_dir)
        {
            // 追加セルは depth 位置から奥側へ半セルだけ進めた位置で選ぶ。
            const float3 far_probe_pos_ws = pixel_pos_ws + surface_view_dir_ws * half_cell_size;
            uint far_global_cell_index = 0;
            if(FspTryGetGlobalCellIndexFromWorldPos(far_probe_pos_ws, cascade_index, far_global_cell_index))
            {
                FspRegisterVisibleCell(far_global_cell_index);
            }
        }
    }
}

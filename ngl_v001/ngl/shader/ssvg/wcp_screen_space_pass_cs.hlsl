
#if 0

wcp_screen_space_pass_cs.hlsl

ハードウェア深度バッファをもとにWorldCacheProbeの処理をする.
可視サーフェイス上にあるWcp要素リストの生成.

#endif

#define TILE_WIDTH 16

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;


// ThreadGroupタイル単位でスキップする最適化のグループタイル幅. 1より大きい数値で実行.
#define THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH 4

// SharedMem上のタイルで簡易重複除去をする際のサイズ.
#define REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH 4
groupshared uint2 shared_work[TILE_WIDTH*TILE_WIDTH];

// DepthBufferに対してDispatch.
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 camera_pos = GetViewPosFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_ssvg.tex_hw_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

    #if 1 < THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH
        // 適当なTile単位処理スキップ軽量化.
        const uint skip_tile_size = THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH;// SxS個のタイルグループ毎に1Fに1タイルだけ処理するシンプル軽量化.
        const uint tile_skip_id_x = gid.x%skip_tile_size;
        const uint tile_skip_id_y = gid.y%skip_tile_size;

        const uint skip_frame_id = cb_ssvg.frame_count % (skip_tile_size*skip_tile_size);
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

    shared_work[gindex] = uint2(~uint(0), 0);// 初期無効値.
        
    // 深度->PixelWorldPosition
    const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, cb_ngl_sceneview.cb_proj_mtx);
    const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));

    // PixelWorldPosition->VoxelCoord
    const float3 voxel_coordf = (pixel_pos_ws - cb_ssvg.wcp.grid_min_pos) * cb_ssvg.wcp.cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < cb_ssvg.wcp.grid_resolution))
    {
        int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.wcp.grid_toroidal_offset, cb_ssvg.wcp.grid_resolution);
        uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.wcp.grid_resolution);

        shared_work[gindex] = uint2(voxel_index, 1);
    }


    GroupMemoryBarrierWithGroupSync();
    // シンプルに小Tile毎に最小インデックスの要素との一致をチェックしてマージと除去をする.
    // 後段のAtomic操作を減らすことが目的.
    if(0 == (gtid.x%REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH) && 0 == (gtid.y%REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH))
    {
        for(int ix = 0; ix < REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH; ++ix)
        {
            for(int iy = 0; iy < REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH; ++iy)
            {
                const uint check_index = (gtid.y + iy)*TILE_WIDTH + (gtid.x + ix);
                if(check_index == gindex || check_index >= (TILE_WIDTH*TILE_WIDTH))
                    continue;
                
                // Voxelの一致チェック.
                if(shared_work[gindex].x == shared_work[check_index].x)
                {
                    shared_work[gindex].y = 0;// 重複するのでこの要素による書き込みは不要にする.
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // shared memからバッファ書き込み解決. ここで重複要素への書き込みをマージしてAtomic操作の衝突を最小化したい.
    const uint voxel_index = shared_work[gindex].x;
    const uint valid_flag = shared_work[gindex].y;
    if(~uint(0) != voxel_index)
    {
        if(0 != valid_flag)
        {
            // Visible判定フレーム番号を書き込み.
            uint old_atomic_work;
            InterlockedExchange(RWWcpProbeBuffer[voxel_index].atomic_work, cb_ssvg.frame_count, old_atomic_work);

            // 交換前の値でVisible判定フレーム番号が現在フレームと異なるならリストへ登録. 別スレッドで同じVoxelを処理している場合の重複を除去する.
            if(cb_ssvg.frame_count !=  old_atomic_work)
            {
                int current_visible_count;
                InterlockedAdd(RWSurfaceProbeCellList[0], 1, current_visible_count);
                if(cb_ssvg.wcp_visible_voxel_buffer_size > current_visible_count)
                {
                    // 追加可能であれば登録. 登録位置はindex0のカウンタを除いた位置.
                    RWSurfaceProbeCellList[current_visible_count + 1] = voxel_index;
                }
                else
                {
                    // サイズオーバーの場合はカウンタを戻す.
                    InterlockedAdd(RWSurfaceProbeCellList[0], -1);
                }
            }
        }
    }

}
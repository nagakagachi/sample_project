
#if 0

ss_voxelize_cs.hlsl

ハードウェア深度バッファからリニア深度バッファを生成

#endif

#define TILE_WIDTH 16

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;


// ThreadGroupタイル単位でスキップする最適化のグループタイル幅. 1より大きい数値で実行.
#define THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH 4

// SharedMem上のタイルで簡易重複除去をする際のサイズ.
#define REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH 4
groupshared uint4 shared_obm_bitmask_addr[TILE_WIDTH*TILE_WIDTH];

// DepthBufferに対してDispatch.
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

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
    float view_z = ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.x / (d * ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.y + ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.z);

    // Skyチェック.
    if(65535.0 <= abs(view_z))
        return;

    shared_obm_bitmask_addr[gindex] = uint4(~uint(0), 0, 0, 0);// 初期無効値.
        
    // 深度->PixelWorldPosition
    const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
    const float3 pixel_pos_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));

    // PixelWorldPosition->VoxelCoord
    const float3 voxel_coordf = (pixel_pos_ws - cb_ssvg.grid_min_pos) * cb_ssvg.cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < cb_ssvg.base_grid_resolution))
    {
        int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.grid_toroidal_offset, cb_ssvg.base_grid_resolution);
        uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.base_grid_resolution);

        {
            // 占有ビットマスク.
            const float3 voxel_coord_frac = frac(voxel_coordf);
            const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_obm_per_voxel_resolution);

            uint bitcell_u32_offset;
            uint bitcell_u32_bit_pos;
            calc_obm_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);

            shared_obm_bitmask_addr[gindex] = uint4(voxel_index, 1, bitcell_u32_offset, (1 << bitcell_u32_bit_pos));
        }
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
                if(shared_obm_bitmask_addr[gindex].x == shared_obm_bitmask_addr[check_index].x)
                {
                    shared_obm_bitmask_addr[gindex].y = 0;// Voxelの占有ビット書き込み不要にする.

                    // u32オフセットも一致する場合はビットマスクをマージ.
                    if(shared_obm_bitmask_addr[gindex].z == shared_obm_bitmask_addr[check_index].z)
                    {
                        shared_obm_bitmask_addr[gindex].w |= shared_obm_bitmask_addr[check_index].w;
                        shared_obm_bitmask_addr[check_index].x = ~uint(0);// Voxelも書き込みu32オフセットも一致するため完全にマージして無効化.
                    }
                }   
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // shared memからバッファ書き込み解決. ここで重複要素への書き込みをマージしてAtomic操作の衝突を最小化したい.
    const uint voxel_index = shared_obm_bitmask_addr[gindex].x;
    const uint valid_flag = shared_obm_bitmask_addr[gindex].y;
    const uint bitmask_u32_offset = shared_obm_bitmask_addr[gindex].z;
    const uint bitmask_append = shared_obm_bitmask_addr[gindex].w;
    if(~uint(0) != voxel_index)
    {
        const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
        const uint obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);
    
        // 詳細ジオメトリを占有ビット書き込み.
        InterlockedOr(RWOccupancyBitmaskVoxel[obm_addr + bitmask_u32_offset], bitmask_append);

        if(0 != valid_flag)
        {
            // Memo.
            // 以下でVoxel固有データ部を更新しているが, 今後実装予定の動的オブジェクトによる占有度除去に伴う処理でも更新が必要な点に注意.

            // 非Emptyフラグを0bit目, それ以降にVisible判定フレーム番号を書き込み.
            const uint visible_check_frame_count = mask_obm_voxel_unique_data_last_visible_frame(cb_ssvg.frame_count);
            ObmVoxelUniqueData new_unique_data;
            new_unique_data.is_occupied = 1;
            new_unique_data.last_visible_frame = visible_check_frame_count;
            const uint set_bits = build_obm_voxel_unique_data(new_unique_data);
            uint old_unique_bits;
            InterlockedExchange(RWOccupancyBitmaskVoxel[unique_data_addr], set_bits, old_unique_bits);

            // old_unique_dataを展開
            ObmVoxelUniqueData old_unique_data;
            parse_obm_voxel_unique_data(old_unique_data, old_unique_bits);

            // 交換前の値でVisible判定フレーム番号が現在フレームと異なるならリストへ登録. 別スレッドで同じVoxelを処理している場合の重複を除去する.
            if(visible_check_frame_count !=  old_unique_data.last_visible_frame)
            {
                int current_visible_count;
                InterlockedAdd(RWVisibleVoxelList[0], 1, current_visible_count);
                if(cb_ssvg.update_probe_work_count > current_visible_count)
                {
                    // 追加可能であれば登録. 登録位置はindex0のカウンタを除いた位置.
                    RWVisibleVoxelList[current_visible_count + 1] = voxel_index;
                }
                else
                {
                    // サイズオーバーの場合はカウンタを戻す.
                    InterlockedAdd(RWVisibleVoxelList[0], -1);
                }
            }
        }
    }

}
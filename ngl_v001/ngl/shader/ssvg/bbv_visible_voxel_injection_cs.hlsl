
#if 0

bbv_visible_voxel_injection_cs.hlsl

深度バッファをもとに可視表面のVoxel情報をBbvに充填する.
また, フレームでの可視Voxel処理用リストの生成.

ViewとしてはPerspectiveなMainViewに加えてShadowMapViewも同一シェーダでInjectionしたい.

#endif

#define TILE_WIDTH 16

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

// MainViewの情報.
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

// Injection元のDepthDeputhBufferのView情報.
ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_bbv_surface_injection_view_info;

Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;


// ThreadGroupタイル単位でスキップする最適化のグループタイル幅. 1より大きい数値で実行.
#define THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH 0

// SharedMem上のタイルで簡易重複除去をする際のサイズ.
#define REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH 4
groupshared uint4 shared_bbv_bitmask_addr[TILE_WIDTH*TILE_WIDTH];

// DepthBufferに対してDispatch.
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // メインビューの情報.
	const float3 main_view_camera_dir = GetViewDirFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);
	const float3 main_view_camera_pos = GetViewPosFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);


	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_ssvg.tex_hw_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

    #if 1 < THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH
        // Tile単位処理スキップ軽量化.
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


    float d = TexHardwareDepth.Load(int3(dtid.xy, 0)).r;
    // DepthBufferに紐づいたView情報で復元.
    float view_z = calc_view_z_from_ndc_z(d, cb_bbv_surface_injection_view_info.cb_ndc_z_to_view_z_coef);

    // 可視表面のbbv充填.
    {
        shared_bbv_bitmask_addr[gindex] = uint4(~uint(0), 0, 0, 0);// 初期無効値.
        
        // 空ではない場合のみ.
        if(65535.0 > abs(view_z))
        {
            // 深度->PixelWorldPosition
            // DepthBufferに紐づいたView情報で復元.
            const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, cb_bbv_surface_injection_view_info.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_bbv_surface_injection_view_info.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));

            // PixelWorldPosition->VoxelCoord
            const float3 voxel_coordf = (pixel_pos_ws - cb_ssvg.bbv.grid_min_pos) * cb_ssvg.bbv.cell_size_inv;
            const int3 voxel_coord = floor(voxel_coordf);
            if(all(voxel_coord >= 0) && all(voxel_coord < cb_ssvg.bbv.grid_resolution))
            {
                int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);
                uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution);
                {
                    // 占有ビットマスク.
                    const float3 voxel_coord_frac = frac(voxel_coordf);
                    const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_bbv_per_voxel_resolution);
                    uint bitcell_u32_offset, bitcell_u32_bit_pos;
                    calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);
                    shared_bbv_bitmask_addr[gindex] = uint4(voxel_index, 1, bitcell_u32_offset, (1 << bitcell_u32_bit_pos));
                }
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
                    if(shared_bbv_bitmask_addr[gindex].x == shared_bbv_bitmask_addr[check_index].x)
                    {
                        // Brick毎の処理を最小化するためにBrick重複する他の要素はフラグを落とす.
                        shared_bbv_bitmask_addr[check_index].y = 0;

                        // u32オフセットも一致する場合はビットマスクをマージ.
                        if(shared_bbv_bitmask_addr[gindex].z == shared_bbv_bitmask_addr[check_index].z)
                        {
                            shared_bbv_bitmask_addr[gindex].w |= shared_bbv_bitmask_addr[check_index].w;// マージ
                            shared_bbv_bitmask_addr[check_index].x = ~uint(0);// Voxelも書き込みu32オフセットも一致するため完全にマージして無効化.
                        }
                    }   
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();

        // shared memからバッファ書き込み解決. ここで重複要素への書き込みをマージしてAtomic操作の衝突を最小化したい.
        const uint voxel_index = shared_bbv_bitmask_addr[gindex].x;
        const uint is_unique_brick_flag = shared_bbv_bitmask_addr[gindex].y;
        const uint bitmask_u32_offset = shared_bbv_bitmask_addr[gindex].z;
        const uint bitmask_append = shared_bbv_bitmask_addr[gindex].w;
        if(~uint(0) != voxel_index)
        {
            const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
        
            // 詳細ジオメトリをbitmask書き込み.
            InterlockedOr(RWBitmaskBrickVoxel[bbv_addr + bitmask_u32_offset], bitmask_append);
            // 表面が存在し非ゼロbitがあるコンポーネントのビットを立てたOccupiedフラグをAtomic OR で書き込む.
            // 中空になって占有されなくなったBrickのOccupiedフラグの除去は, bitmaskの除去といっしょに別シェーダで実行される.
            InterlockedOr(RWBitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(voxel_index)], (1 << bitmask_u32_offset));
            
            // ここから先はBrick単位で行いたい処理のAtomic操作を最小化するための分岐.
            if(0 != is_unique_brick_flag)
            {
                const uint visible_check_frame_count = mask_bbv_voxel_unique_data_last_visible_frame(cb_ssvg.frame_count);                
                // Brickの固有データ部のフレームインデックスを最新でAtomic交換する. ここで以前の値が最新の値と異なるのであればこのスレッドがこのフレームでこのBrickに対する唯一の処理を実行する権利を得る.
                uint old_last_visible_frame;
                InterlockedExchange(RWBitmaskBrickVoxel[bbv_voxel_brick_work_addr(voxel_index)], visible_check_frame_count, old_last_visible_frame);
                // bitmask書き込みがあったBrickを重複無しでリストアップするためのスタックへ追加.
                if(visible_check_frame_count !=  old_last_visible_frame)
                {
                    int current_visible_count;
                    InterlockedAdd(RWVisibleVoxelList[0], 1, current_visible_count);
                    if(cb_ssvg.bbv_visible_voxel_buffer_size > current_visible_count)
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

}
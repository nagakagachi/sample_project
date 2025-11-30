
#if 0

bbv_generate_hollow_voxel_info_cs.hlsl

ハードウェア深度バッファより手前にある中空になったBitmaskBrickVoxelを除去するための情報を生成する.

#endif

#define TILE_WIDTH 16

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

// Injection元のDepthDeputhBufferのView情報.
ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;

Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;


// ThreadGroupタイル単位でスキップする最適化のグループタイル幅. 1より大きい数値で実行.
#define THREAD_GROUP_SKIP_OPTIMIZE_GROUP_TILE_WIDTH 4

// SharedMem上のタイルで簡易重複除去をする際のサイズ.(小タイルで重複処理する場合)
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
	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 camera_pos = GetViewPosFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    // 範囲チェック.
    if(any(dtid.xy >= cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw))
    {
        return;
    }

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

    // ハードウェア深度取得. AtlasTexture対応のためオフセット考慮.
    float d = TexHardwareDepth.Load(int3(dtid.xy + cb_injection_src_view_info.cb_view_depth_buffer_offset_size.xy, 0)).r;
    float view_z = min(65535.0, calc_view_z_from_ndc_z(d, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef));

    shared_bbv_bitmask_addr[gindex] = uint4(~uint(0), 0, 0, 0);// 初期無効値.
    
    const float2 screen_uv = (float2(dtid.xy) + float2(0.5, 0.5)) / float2(cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw);
    // Orthoも含めて対応するためPositionを直接復元.
    const float3 pixel_pos_ws = mul(cb_injection_src_view_info.cb_view_inv_mtx, float4(CalcViewSpacePosition(screen_uv, view_z, cb_injection_src_view_info.cb_proj_mtx), 1.0));

    // Note. ShadowMap等のOrthoでは投影ベクトルのほうが正しいので後で修正する.
    const float3 to_pixel_vec_ws = pixel_pos_ws - camera_pos;
    const float3 ray_dir_ws = normalize(to_pixel_vec_ws);

    // 深度バッファの手前までレイトレース.
    // Note:適当な固定値ではなく, DDA相当の計算で1セル分バックトレースしたい
    const float trace_distance = dot(ray_dir_ws, to_pixel_vec_ws) - cb_ssvg.bbv.cell_size*k_bbv_per_voxel_resolution_inv*0.9;
        
    int hit_voxel_index = -1;
    float4 debug_ray_info;
    // Trace最適化検証.
    float4 curr_ray_t_ws = trace_bbv(
        hit_voxel_index, debug_ray_info,
        camera_pos, ray_dir_ws, trace_distance, 
        cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
        cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

    if(0.0 <= curr_ray_t_ws.x)
    {
        const float3 hit_pos_ws = camera_pos + ray_dir_ws * (curr_ray_t_ws.x + 0.001);// ヒット位置は表面なので除去したいVoxelに侵入するためオフセット.

        // PixelWorldPosition->VoxelCoord
        const float3 voxel_coordf = (hit_pos_ws - cb_ssvg.bbv.grid_min_pos) * cb_ssvg.bbv.cell_size_inv;
        const int3 voxel_coord = floor(voxel_coordf);
        if(all(voxel_coord >= 0) && all(voxel_coord < cb_ssvg.bbv.grid_resolution))
        {
            int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution);
            uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution);

            {
                // 占有ビットマスク.
                const float3 voxel_coord_frac = frac(voxel_coordf);
                const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_bbv_per_voxel_resolution);

                uint bitcell_u32_offset;
                uint bitcell_u32_bit_pos;
                calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);

                shared_bbv_bitmask_addr[gindex] = uint4(voxel_index, 1, bitcell_u32_offset, (1u << bitcell_u32_bit_pos));
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    #if 0
        // シンプルに小Tile毎に最小インデックスの要素との一致をチェックしてマージと除去をする.後段のAtomic操作を減らすことが目的.
        // 小タイルの0番目が担当.
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
                        // u32オフセットも一致する場合はビットマスクをマージ.
                        if(shared_bbv_bitmask_addr[gindex].z == shared_bbv_bitmask_addr[check_index].z)
                        {
                            shared_bbv_bitmask_addr[gindex].w |= shared_bbv_bitmask_addr[check_index].w;// マージ.
                            shared_bbv_bitmask_addr[check_index].x = ~uint(0);// Voxelも書き込みu32オフセットも一致するため完全にマージして無効化.
                        }
                    }   
                }
            }
        }
    #else
        // シンプルにshared_bbv_bitmask_addrを線形探索して x,zが一致する要素はマージし, インデックスが若い方のみを残す.
        //const uint k_reduce_tile_size = REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH*REDUCE_ATOMIC_WRITE_OPTIMIZE_TILE_WIDTH;
        const uint k_reduce_tile_size = TILE_WIDTH*TILE_WIDTH;
        if(0 == (gindex%(k_reduce_tile_size)))
        {
            for(int i = gindex; i < k_reduce_tile_size-1; ++i)
            {
                const uint base_voxel_index = shared_bbv_bitmask_addr[i].x;
                const uint base_bitmask_u32_offset = shared_bbv_bitmask_addr[i].z;
                if(~uint(0) == base_voxel_index)
                    continue;

                for(int j = i + 1; j < k_reduce_tile_size; ++j)
                {
                    // VoxelIndex, u32オフセットが一致する場合はマージ.
                    if((base_voxel_index == shared_bbv_bitmask_addr[j].x) &&
                        (base_bitmask_u32_offset == shared_bbv_bitmask_addr[j].z))
                    {
                        // マージ.
                        shared_bbv_bitmask_addr[i].w |= shared_bbv_bitmask_addr[j].w;
                        // 無効化.
                        shared_bbv_bitmask_addr[j].x = ~uint(0);
                    }
                }
            }
        }
    #endif

    GroupMemoryBarrierWithGroupSync();

    // shared memからバッファ書き込み解決. ここで重複要素への書き込みをマージしてAtomic操作の衝突を最小化したい.
    const uint voxel_index = shared_bbv_bitmask_addr[gindex].x;
    //const uint valid_flag = shared_bbv_bitmask_addr[gindex].y;
    const uint bitmask_u32_offset = shared_bbv_bitmask_addr[gindex].z;
    const uint bitmask_append = shared_bbv_bitmask_addr[gindex].w;

    // スタックに追加.
    if(~uint(0) != voxel_index)
    {
        int current_visible_count;
        InterlockedAdd(RWRemoveVoxelList[0], 1, current_visible_count);
        if(cb_ssvg.bbv_hollow_voxel_buffer_size > current_visible_count)
        {
            // 登録位置はindex0のカウンタを除いた位置(+1).
            const int target_index = (current_visible_count + 1) * k_component_count_RemoveVoxelList;
            // 追加可能であれば登録.
            RWRemoveVoxelList[(target_index)] = voxel_index;
            RWRemoveVoxelList[(target_index) + 1] = bitmask_u32_offset;
            RWRemoveVoxelList[(target_index) + 2] = bitmask_append;
            RWRemoveVoxelList[(target_index) + 3] = 0;// 予備.
        }
        else
        {
            // サイズオーバーの場合はカウンタを戻す.
            InterlockedAdd(RWRemoveVoxelList[0], -1);
        }
    }
}
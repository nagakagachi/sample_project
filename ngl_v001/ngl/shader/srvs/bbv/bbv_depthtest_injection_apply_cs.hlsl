
#if 0

bbv_depthtest_injection_apply_cs.hlsl

DepthTest ベース更新向けの Legacy 相当 Injection。
Legacy Injection と独立した専用シェーダとして維持し、
DepthTest 側の挙動調整が Legacy 側へ影響しないようにする。

#endif

#define TILE_WIDTH 16

#include "../srvs_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

// Injection元のDepthDeputhBufferのView情報.
ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;

Texture2D			TexHardwareDepth;


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
        const uint skip_frame_id = cb_srvs.frame_count % (skip_tile_size*skip_tile_size);
        const uint skip_frame_id_y = skip_frame_id / (skip_tile_size);
        const uint skip_frame_id_x = skip_frame_id % (skip_tile_size);
        if((tile_skip_id_x != skip_frame_id_x) || (tile_skip_id_y != skip_frame_id_y))
        {
            return;
        }
    #endif

    // ハードウェア深度取得. AtlasTexture対応のためオフセット考慮.
    float d = TexHardwareDepth.Load(int3(dtid.xy + cb_injection_src_view_info.cb_view_depth_buffer_offset_size.xy, 0)).r;

    // 可視表面のbbv充填.
    shared_bbv_bitmask_addr[gindex] = uint4(~uint(0), 0, 0, 0);// 初期無効値.
    
    // 無限遠ピクセルのチェック. ReverseZを考慮して0 < d < 1.
    if(0.0 < d && d < 1.0)
    {
        // DepthBufferに紐づいたView情報で復元.
        float view_z = calc_view_z_from_ndc_z(d, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef);
        const float2 near_far_plane_d = GetNearFarPlaneDepthFromProjectionMatrix(cb_injection_src_view_info.cb_proj_mtx);
        const float near_plane_view_z = calc_view_z_from_ndc_z(near_far_plane_d.x, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef);
        
        // 深度->PixelWorldPosition
        // DepthBufferに紐づいたView情報で復元.
        const float2 screen_uv = (float2(dtid.xy) + float2(0.5, 0.5)) / float2(cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw);
        // Orthoも含めて対応するためPositionを直接復元.
        float3 pixel_pos_ws = mul(cb_injection_src_view_info.cb_view_inv_mtx, float4(CalcViewSpacePosition(screen_uv, view_z, cb_injection_src_view_info.cb_proj_mtx), 1.0));

        // near平面上の同一ピクセル点を始点にすると、
        // Perspectiveでは放射状、Orthoでは平行方向のレイが得られる。
        const float3 view_ray_origin_ws = mul(cb_injection_src_view_info.cb_view_inv_mtx, float4(CalcViewSpacePosition(screen_uv, near_plane_view_z, cb_injection_src_view_info.cb_proj_mtx), 1.0));
        const float3 to_pixel_vec_ws = pixel_pos_ws - view_ray_origin_ws;
        const float to_pixel_len_sq = dot(to_pixel_vec_ws, to_pixel_vec_ws);
        if(to_pixel_len_sq > 1e-10)
        {
            // 表面注入がRemovalで落ちやすいケース向けに、固定ワールド距離だけ視線奥へシフト.
            pixel_pos_ws += normalize(to_pixel_vec_ws) * cb_srvs.bbv_depthtest_injection_world_offset;
        }


        // PixelWorldPosition->VoxelCoord
        const float3 voxel_coordf = (pixel_pos_ws - cb_srvs.bbv.grid_min_pos) * cb_srvs.bbv.cell_size_inv;
        const int3 voxel_coord = floor(voxel_coordf);
        if(all(voxel_coord >= 0) && all(voxel_coord < cb_srvs.bbv.grid_resolution))
        {
            const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_resolution);
            const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_srvs.bbv.grid_resolution);
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
    const uint bitmask_u32_offset = shared_bbv_bitmask_addr[gindex].z;
    const uint bitmask_append = shared_bbv_bitmask_addr[gindex].w;
    if(~uint(0) != voxel_index)
    {
        const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
    
        // 詳細ジオメトリをbitmask書き込み.
        // Brick / HiBrick count は後段の集計パスで再構築するため、ここでは bitmask 更新だけ行う。
        InterlockedOr(RWBitmaskBrickVoxel[bbv_addr + bitmask_u32_offset], bitmask_append);
    }
}

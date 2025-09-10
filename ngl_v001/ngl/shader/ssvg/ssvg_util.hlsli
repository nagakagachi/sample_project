
#if 0

ss_voxelize_util.hlsli

#endif

#include "../include/math_util.hlsli"

// Cpp側と一致させる.
// Voxelの占有度合いをビットマスク近似する際の1軸の解像度. 2の冪でなくても良い.
#define k_per_voxel_occupancy_reso (8)
#define k_per_voxel_occupancy_bit_count (k_per_voxel_occupancy_reso*k_per_voxel_occupancy_reso*k_per_voxel_occupancy_reso)
#define k_per_voxel_occupancy_u32_count ((k_per_voxel_occupancy_bit_count + 31) / 32)


struct DispatchParam
{
    int3 BaseResolution;
    uint Flag;

    float3 GridMinPos;
    float CellSize;
    int3 GridToroidalOffset;
    float CellSizeInv;

    int3 GridToroidalOffsetPrev;
    int Dummy0;
    
    int3 GridCellDelta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
    int Dummy1;

    int2 TexHardwareDepthSize;
};


#define VOXEL_ADDR_MODE 0
// Coordからアドレス計算(リニア).
uint voxel_coord_to_addr_linear(int3 coord, int3 resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}
// アドレスからCoord計算(リニア).
int3 addr_to_voxel_coord_linear(uint addr, int3 resolution)
{
    int z = addr / (resolution.x * resolution.y);
    addr -= z * (resolution.x * resolution.y);
    int y = addr / resolution.x;
    addr -= y * resolution.x;
    int x = addr;
    return int3(x, y, z);
}

// Coordからアドレス計算.
uint voxel_coord_to_addr(int3 coord, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
    #endif
}
// アドレスからCoord計算.
int3 addr_to_voxel_coord(uint addr, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        int z = addr / (resolution.x * resolution.y);
        addr -= z * (resolution.x * resolution.y);
        int y = addr / resolution.x;
        addr -= y * resolution.x;
        int x = addr;
    #endif
    return int3(x, y, z);
}


// リニアなVoxel座標をループするToroidalマッピングに変換する.
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution)
{
    return (voxel_coord + toroidal_offset) % resolution;
}

// Occupancy Bitmask Voxelの内部座標を元にバッファの該当Voxelブロック内のオフセットと読み取りビット位置を計算.
void calc_occupancy_bitmask_voxel_inner_bit_info(out uint out_u32_offset, out uint out_bit_location, uint3 bit_position_in_voxel)
{
    const uint3 bit_pos = (bit_position_in_voxel);
    const uint bit_linear_pos = bit_pos.x + (bit_pos.y * k_per_voxel_occupancy_reso) + (bit_pos.z * (k_per_voxel_occupancy_reso * k_per_voxel_occupancy_reso));
    out_u32_offset = bit_linear_pos / 32;
    out_bit_location = bit_linear_pos - (out_u32_offset * 32);
}





// レイの始点終点セットアップ. 領域AABBの内部または表面から開始するための始点終点のt値( origin + dir * t) を計算.
// aabb_min, aabb_max, ray_origin, ray_end のすべての空間が一致していればどの空間の情報でも適切な結果を返す(World空間でもCell基準空間でも).
bool calc_ray_t_offset_for_aabb(out float out_aabb_clamped_origin_t, out float out_aabb_clamped_end_t, out float3 out_ray_dir_inv, float3 aabb_min, float3 aabb_max, float3 ray_origin, float3 ray_end)
{
    const float3 ray_d_c = ray_end - ray_origin;
    const float3 ray_dir_c = normalize(ray_d_c);
    const float ray_len_c = dot(ray_dir_c, ray_d_c);

    // Inv Dir.
    const float k_nearly_zero_threshold = 1e-7;
    const float k_float_max = 1e20;
    out_ray_dir_inv = select( k_nearly_zero_threshold > abs(ray_dir_c), float3(k_float_max, k_float_max, k_float_max), 1.0f / ray_dir_c);

    const float3 t_to_min = (aabb_min - ray_origin) * out_ray_dir_inv;
    const float3 t_to_max = (aabb_max - ray_origin) * out_ray_dir_inv;
    const float3 t_near_v3 = min(t_to_min, t_to_max);
    const float3 t_far_v3 = max(t_to_min, t_to_max);
    const float t_near = max(t_near_v3.x, max(t_near_v3.y, t_near_v3.z));
    const float t_far = min(t_far_v3.x, min(t_far_v3.y, t_far_v3.z));

    // GridBoxとの交点が存在しなければ早期終了. t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
    if (0.0f > t_far || t_near >= t_far || ray_len_c < t_near)
        return false;

    // 結果を返す. このt値で origin + dir * t を計算すればそれぞれ始点と終点がAABB空間内にクランプされた座標になる.
    out_aabb_clamped_origin_t = (0.0 > t_near) ? 0.0 : t_near;
    out_aabb_clamped_end_t = (ray_len_c < t_far) ? ray_len_c : t_far;
    return true;
};

// トレース実行.
//  return : [0.0, t_of_world_space) (ヒット無しの場合は負数).
float trace_ray_vs_occupancy_bitmask_voxel(
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 grid_toroidal_offset, Buffer<uint> occupancy_bitmask_voxel)
{
    const float3 grid_box_min = float3(0.0, 0.0, 0.0);
    const float3 grid_box_max = float3(grid_resolution);
    const int3 grid_box_cell_max = int3(grid_resolution - 1);

    // 後段のDDAのためにCell単位空間の始点終点に変換.
    const float3 ray_begin_c = (ray_origin_ws - grid_min_ws) * (1.0f / cell_width_ws);
    const float3 ray_end_c = ((ray_origin_ws + ray_dir_ws * trace_distance_ws) - grid_min_ws) * (1.0f / cell_width_ws);

    float ray_begin_t;
    float ray_end_t;
    float3 ray_dir_inv_c;
    // レイセットアップ.
    if(!calc_ray_t_offset_for_aabb(ray_begin_t, ray_end_t, ray_dir_inv_c, grid_box_min, grid_box_max, ray_begin_c, ray_end_c))
    {
        return -1.0;// ヒット無し.
    }

    const float ray_d_c_len = ray_end_t - ray_begin_t;// トレース範囲の距離.
    const float ray_d_c_len_inv = 1.0 / ray_d_c_len;

    // Inv Dir.
    const float3 dir_sign = sign(ray_dir_ws);// +1 : positive component or zero, -1 : negative component.
    const float3 cell_delta = dir_sign * ray_dir_inv_c;

    // Grid内にクランプしたトレース始点終点.
    float3 ray_p0_clamped_c = ray_begin_c + ray_begin_t * ray_dir_ws;
    float3 ray_p1_clamped_c = ray_begin_c + ray_end_t * ray_dir_ws;

    // 0ベースでのトラバースCell範囲.
    const int3 trace_begin_cell = min(floor(ray_p0_clamped_c), grid_box_cell_max);
    const int3 trace_end_cell = min(floor(ray_p1_clamped_c), grid_box_cell_max);
    const int3 trance_cell_range = abs(trace_end_cell - trace_begin_cell);

    // 始点からの最初のステップt.
    const float3 cell_delta_offset = abs((floor(ray_p0_clamped_c) + max(dir_sign, float3(0,0,0)) - ray_p0_clamped_c) * ray_dir_inv_c);

    float curr_ray_t = 1.0;// GridAABBでクランプされたレイの長さ ray_d_c_len で正規化した値. 1.0が終端.
    int3 total_cell_step = int3(0,0,0);
    float3 cell_delta_accum = float3(0,0,0);
    // DDAのトラバースであるため, 有効ヒットがあれば即座に最近接として終了.
    for (;1.0 <= curr_ray_t;)
    {
        const float cur_cell_delta = min(cell_delta_accum.x, min(cell_delta_accum.y, cell_delta_accum.z));

        // 到達Cell
        const int3 trace_cell_id = trace_begin_cell + int3(dir_sign) * total_cell_step;
        // 読み取り用のマッピングをして読み取り.
        const uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal_mapping(trace_cell_id, grid_toroidal_offset, grid_resolution), grid_resolution);

        // 1Voxelを構成するビットマスク情報を走査.
        for(int i = 0; i < k_per_voxel_occupancy_u32_count; ++i)
        {
            const uint voxel_elem_bitmask = occupancy_bitmask_voxel[voxel_addr * k_per_voxel_occupancy_u32_count + i];
            // 現状は内部ビットまでは確認せずに内部に非ゼロビットがひとつでもあればヒット扱い.
            if(0 == voxel_elem_bitmask)
                continue;
            
            // ヒット判定処理パラメータ構築. このトレースではcur_cell_deltaが直接レイ上の位置になるため, レイの長さで正規化した値を渡す.
            const float ray_t = cur_cell_delta * ray_d_c_len_inv;
            // CellId範囲チェックとは別にt値のチェック. CellID範囲チェックだけでは広いルート階層換算での終了判定なので実際には線分の範囲外になっても継続してしまうため.
            if (1.0f > ray_t && curr_ray_t > ray_t)
                curr_ray_t = ray_t;// 最近接 t を更新.
        }

        // 次のCellへ移動.
        {
            cell_delta_accum = cell_delta_offset + float3(total_cell_step) * cell_delta;

            // xyzで最小値コンポーネントを探す.
            int3 prev_cell_step = select(cell_delta_accum <= min(float3(cell_delta_accum.y, cell_delta_accum.z, cell_delta_accum.x), float3(cell_delta_accum.z, cell_delta_accum.x, cell_delta_accum.y)), int3(1,1,1), int3(0,0,0));
            if (true)
            {
                // 精密化処理(オプション).
                // 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先.). 
                // この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
                int tmp = prev_cell_step.x;
                prev_cell_step.y = (0 < tmp) ? 0 : prev_cell_step.y;
                tmp += prev_cell_step.y;
                prev_cell_step.z = (0 < tmp) ? 0 : prev_cell_step.z;
            }
            // ステップは整数ベースで進める.
            total_cell_step += prev_cell_step;

            if(any(trance_cell_range < total_cell_step))
                break;// 範囲外で終了.
        }
    }
    // ヒットがあればワールド空間のt値, なければヒット無しとして負数-1.0を返す.
    //  AABB ClampedRay上のヒット位置t -> Cell基準空間t -> ワールド空間t.
    return (1.0 > curr_ray_t) ? (curr_ray_t * ray_d_c_len + ray_begin_t) * cell_width_ws : -1.0;
}



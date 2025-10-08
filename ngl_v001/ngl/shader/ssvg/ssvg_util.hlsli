#ifndef NGL_SHADER_SSVG_UTIL_H
#define NGL_SHADER_SSVG_UTIL_H

#if 0

ssvg_util.hlsli

#endif

#include "../include/math_util.hlsli"

    // シェーダとCppで一致させる.
    // ObmVoxel単位の固有データ部のu32単位数.ジオメトリを表現する占有ビットマスクとは別に荒い単位で保持するデータ. レイアウトの簡易化のためビット単位ではなくu32単位.
    #define k_obm_common_data_u32_count (1)
    // ObmVoxel単位の占有ビットマスク解像度. 2の冪でなくても良い.
    #define k_obm_per_voxel_resolution (8)
    #define k_obm_per_voxel_bitmask_bit_count (k_obm_per_voxel_resolution*k_obm_per_voxel_resolution*k_obm_per_voxel_resolution)
    #define k_obm_per_voxel_occupancy_bitmask_u32_count ((k_obm_per_voxel_bitmask_bit_count + 31) / 32)
    // ObmVoxel単位のデータサイズ(u32単位)
    #define k_obm_per_voxel_u32_count (k_obm_per_voxel_occupancy_bitmask_u32_count + k_obm_common_data_u32_count)

    #define k_obm_per_voxel_resolution_inv (1.0 / float(k_obm_per_voxel_resolution))
    #define k_obm_per_voxel_resolution_vec3i int3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution)

    // probeあたりのOctMap解像度.
    #define k_probe_octmap_width (6)
    // それぞれのOctMapの+側境界に1テクセルボーダーを追加することで全方向に1テクセルのマージンを確保する.
    #define k_probe_octmap_width_with_border (k_probe_octmap_width+2)

    #define k_per_probe_texel_count (k_probe_octmap_width*k_probe_octmap_width)


    // 可視Probe更新時のスキップ数. 0でスキップせずに可視Probeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_VISIBLE_PROBE_SKIP_COUNT 0
    // Probe全体更新のスキップ数. 0でスキップせずにProbeバッファのすべての要素を処理する. 1で1つ飛ばしでスキップ(半分).
    #define FRAME_UPDATE_ALL_PROBE_SKIP_COUNT 60


    // シェーダとCppで一致させる.
    // CoarseVoxelバッファ. ObmVoxel一つ毎の外部データ.
    // 値域によって圧縮表現可能なものがあるが, 現状は簡単のため圧縮せず.
    struct CoarseVoxelData
    {
        uint probe_pos_index;   // ObmVoxel内部でのプローブ位置インデックス. 0は無効, probe_pos_index-1 が実際のインデックス. 値域は 0,k_obm_per_voxel_bitmask_bit_count.
        uint reserved;          // 予備.
    };




// Probe更新系のCS ThreadGroupSize. Indirectのため共有ヘッダに定義.
// SharedMemのサイズ制限のため調整.
#define PROBE_UPDATE_THREAD_GROUP_SIZE 96





//----------------------------------------------------------------------------------------------------
// OccupancyBitmaskVoxel本体. ObmVoxel.
Buffer<uint>		OccupancyBitmaskVoxel;
RWBuffer<uint>		RWOccupancyBitmaskVoxel;

StructuredBuffer<CoarseVoxelData>		CoarseVoxelBuffer;
RWStructuredBuffer<CoarseVoxelData>		RWCoarseVoxelBuffer;

Texture2D       		TexProbeSkyVisibility;
RWTexture2D<float>		RWTexProbeSkyVisibility;
//RWTexture2D<float4>		RWTexProbeSkyVisibility;


Buffer<uint>		VisibleCoarseVoxelList;
RWBuffer<uint>		RWVisibleCoarseVoxelList;


Buffer<float>		UpdateProbeWork;
RWBuffer<float>		RWUpdateProbeWork;
//----------------------------------------------------------------------------------------------------


// Voxel座標からVoxelIndex計算.
uint voxel_coord_to_index(int3 coord, int3 resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}
// VoxelIndexからVoxel座標計算.
int3 index_to_voxel_coord(uint index, int3 resolution)
{
    int z = index / (resolution.x * resolution.y);
    index -= z * (resolution.x * resolution.y);
    int y = index / resolution.x;
    index -= y * resolution.x;
    int x = index;
    return int3(x, y, z);
}
// リニアなVoxel座標をループするToroidalマッピングに変換する.
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution)
{
    return (voxel_coord + toroidal_offset) % resolution;
}

// ObmVoxelの取り扱い.
//----------------------------------------------------------------------------------------------------
// VoxelIndexからアドレス計算. Buffer上の該当Voxelデータの先頭アドレスを返す.
uint obm_voxel_index_to_addr(uint voxel_index)
{
    return voxel_index * k_obm_per_voxel_u32_count;
}
// Voxel毎のデータ部の固有データ先頭アドレス計算.
uint obm_voxel_unique_data_addr(uint voxel_index)
{
    return obm_voxel_index_to_addr(voxel_index) + 0;
}
// Voxel毎のデータ部の占有ビットマスクデータ先頭アドレス計算.
uint obm_voxel_occupancy_bitmask_data_addr(uint voxel_index)
{
    // Voxel毎のデータ部の先頭はVoxel固有データ, 占有ビットマスク の順にレイアウト.
    return obm_voxel_index_to_addr(voxel_index) + k_obm_common_data_u32_count;
}
// Voxel毎の占有ビットマスクのu32単位数.
uint obm_voxel_occupancy_bitmask_uint_count()
{
    return k_obm_per_voxel_occupancy_bitmask_u32_count;
}

// Occupancy Bitmask Voxelの内部座標を元にリニアインデックスを計算.
uint calc_occupancy_bitmask_cell_linear_index(uint3 bit_position_in_voxel)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    return bit_position_in_voxel.x + (bit_position_in_voxel.y * k_obm_per_voxel_resolution) + (bit_position_in_voxel.z * (k_obm_per_voxel_resolution * k_obm_per_voxel_resolution));
}
// calc_occupancy_bitmask_cell_linear_index で計算したリニアインデックスからVoxelブロック内のオフセットと読み取りビット位置を計算.
void calc_occupancy_bitmask_voxel_inner_bit_info_from_linear_index(out uint out_u32_offset, out uint out_bit_location, uint bitmask_cell_linear_index)
{
    out_u32_offset = bitmask_cell_linear_index / 32;
    out_bit_location = bitmask_cell_linear_index - (out_u32_offset * 32);
}
// Occupancy Bitmask Voxelの内部座標を元にバッファの該当Voxelブロック内のオフセットと読み取りビット位置を計算.
void calc_occupancy_bitmask_voxel_inner_bit_info(out uint out_u32_offset, out uint out_bit_location, uint3 bit_position_in_voxel)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint bit_linear_pos = calc_occupancy_bitmask_cell_linear_index(bit_position_in_voxel);

    calc_occupancy_bitmask_voxel_inner_bit_info_from_linear_index(out_u32_offset, out_bit_location, bit_linear_pos);
}
// Occupancy Bitmask Voxelのビットセルインデックスからk_per_voxel_occupancy_reso^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_obm_per_voxel_bitmask_bit_count-1
uint3 calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(uint bit_index)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint3 bit_pos = uint3(bit_index % k_obm_per_voxel_resolution, (bit_index / k_obm_per_voxel_resolution) % k_obm_per_voxel_resolution, bit_index / (k_obm_per_voxel_resolution * k_obm_per_voxel_resolution));
    return bit_pos;
}





// Dispatchパラメータ.
struct SsvgParam
{
    int3 base_grid_resolution;
    uint flag;

    float3 grid_min_pos;
    float cell_size;
    int3 grid_toroidal_offset;
    float cell_size_inv;

    int3 grid_toroidal_offset_prev;
    int dummy0;
    
    int3 grid_move_cell_delta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
    int probe_atlas_texture_base_width;// probeのAtlasを配置するテクスチャの基準幅. 実際はProbe毎のAtlasサイズを乗じたサイズのテクスチャを扱う.


    int3 voxel_dispatch_thread_group_count;// IndirectArg計算のためにVoxel更新ComputeShaderのThreadGroupサイズを格納.
    
    int update_probe_work_count;// 更新プローブ用のワークサイズ.

    int2 tex_hw_depth_size;
    uint frame_count;

    int debug_view_mode;
    int debug_probe_mode;
    float debug_probe_radius;
    float debug_probe_near_geom_scale;
};
ConstantBuffer<SsvgParam> cb_ssvg;


// Voxelデータクリア.
void clear_voxel_data(RWBuffer<uint> occupancy_bitmask_voxel, uint voxel_index)
{
    const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
    // 固有データクリア.
    for(int i = 0; i < k_obm_common_data_u32_count; ++i)
    {
        occupancy_bitmask_voxel[unique_data_addr + i] = 0;
    }

    // 占有ビットマスククリア.
    const uint obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);
    for(int i = 0; i < obm_voxel_occupancy_bitmask_uint_count(); ++i)
    {
        occupancy_bitmask_voxel[obm_addr + i] = 0;
    }
}


// ワールド座標からOBVの値を読み取る.
uint read_occupancy_bitmask_voxel_from_world_pos(Buffer<uint> occupancy_bitmask_voxel, int3 grid_resolution, int3 grid_toroidal_offset, float3 grid_min_pos_world, float cell_size_inv, float3 pos_world)
{
    // WorldPosからVoxelCoordを計算.
    const float3 voxel_coordf = (pos_world - grid_min_pos_world) * cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < grid_resolution))
    {
        int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, grid_toroidal_offset, grid_resolution);
        uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, grid_resolution);

        const uint voxel_obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);
        // 占有ビットマスクの座標.
        const float3 voxel_coord_frac = frac(voxel_coordf);
        const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_obm_per_voxel_resolution);
        // 占有ビットマスクのデータ部情報.
        uint bitmask_u32_offset;
        uint bitmask_u32_bit_pos;
        calc_occupancy_bitmask_voxel_inner_bit_info(bitmask_u32_offset, bitmask_u32_bit_pos, voxel_coord_bitmask_pos);
        const uint bitmask_append = (1 << bitmask_u32_bit_pos);
        // 読み取り.
        return (occupancy_bitmask_voxel[voxel_obm_addr + bitmask_u32_offset] & bitmask_append) ? 1 : 0;
    }

    return 0;
}




//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 効率化版レイトレース.
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 呼び出し側で引数の符号なし uint3 pos に符号付きint3を渡すことでオーバーフローして最大値になるため, 0 <= pos < size の範囲内にあるかをチェックとなる.
bool check_grid_bound(uint3 pos, uint sizeX, uint sizeY, uint sizeZ) {
    return pos.x < sizeX && pos.y < sizeY && pos.z < sizeZ;
}
float3 calc_inverse_ray_direction(float3 ray_dir)
{
    const float k_nearly_zero_threshold = 1e-7;
    const float k_float_max = 1e20;
    // Safety Inverse Dir.
    return select( k_nearly_zero_threshold > abs(ray_dir), float3(k_float_max, k_float_max, k_float_max), 1.0 / ray_dir);
}

// https://github.com/dubiousconst282/VoxelRT
float3 calc_dda_trace_ray_side_distance(float3 ray_pos, float3 ray_dir)
{
    float3 inv_dir = calc_inverse_ray_direction(ray_dir);
    float3 ray_side_distance = ((floor(ray_pos) - ray_pos) + step(0.0, ray_dir)) * inv_dir;
    return ray_side_distance;
}

// ray dir の逆数による次の境界への距離からdda用のステップ用軸選択boolマスクを計算.
bool3 calc_dda_trace_step_mask(float3 ray_side_distance) {
    bool3 mask;
    mask.x = ray_side_distance.x < ray_side_distance.y && ray_side_distance.x < ray_side_distance.z;
    mask.y = !mask.x && ray_side_distance.y < ray_side_distance.z;
    mask.z = !mask.x && !mask.y;
    return mask;
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
    // Safety Inverse Dir.
    out_ray_dir_inv = select( k_nearly_zero_threshold > abs(ray_dir_c), float3(k_float_max, k_float_max, k_float_max), 1.0 / ray_dir_c);

    const float3 t_to_min = (aabb_min - ray_origin) * out_ray_dir_inv;
    const float3 t_to_max = (aabb_max - ray_origin) * out_ray_dir_inv;
    const float3 t_near_v3 = min(t_to_min, t_to_max);
    const float3 t_far_v3 = max(t_to_min, t_to_max);
    const float t_near = max(t_near_v3.x, max(t_near_v3.y, t_near_v3.z));
    const float t_far = min(t_far_v3.x, min(t_far_v3.y, t_far_v3.z));

    // GridBoxとの交点が存在しなければ早期終了. t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
    if (t_far < 0.0 || t_far <= t_near || ray_len_c < t_near)
        return false;

    // 結果を返す. このt値で origin + dir * t を計算すればそれぞれ始点と終点がAABB空間内にクランプされた座標になる.
    out_aabb_clamped_origin_t = max(0.0, t_near);
    out_aabb_clamped_end_t = min(ray_len_c, t_far);
    return true;
};

// OccupancyBitmaskVoxel内部のビットセル単位でのレイトレース.
// https://github.com/dubiousconst282/VoxelRT
int3 trace_bitmask_brick(float3 rayPos, float3 rayDir, float3 invDir, inout bool3 stepMask, 
        Buffer<uint> occupancy_bitmask_voxel, uint voxel_index) 
{
    rayPos = clamp(rayPos, 0.0001, float(k_obm_per_voxel_resolution)-0.0001);

    float3 sideDist = ((floor(rayPos) - rayPos) + step(0.0, rayDir)) * invDir;
    int3 mapPos = int3(floor(rayPos));

    int3 raySign = sign(rayDir);
    #if 1
        const uint obm_bitmask_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);
        do {
            uint bitmask_u32_offset, bitmask_u32_bit_pos;
            calc_occupancy_bitmask_voxel_inner_bit_info(bitmask_u32_offset, bitmask_u32_bit_pos, mapPos);
            bool is_hit = occupancy_bitmask_voxel[obm_bitmask_addr + bitmask_u32_offset] & (1 << bitmask_u32_bit_pos);
            
            if(is_hit) { return mapPos; }

            stepMask = calc_dda_trace_step_mask(sideDist);
            const int3 mapPosDelta = select(stepMask, raySign, 0);
            if(all(mapPosDelta == 0)) {break;}// ここがゼロになって無限ループになる場合がある???  ため安全break.
            
            mapPos += mapPosDelta;
            sideDist += select(stepMask, abs(invDir), 0);
        } while (all(uint3(mapPos) < k_obm_per_voxel_resolution));

        return -1;
    #else
        // デバッグ用に即時ヒット扱い.
        return mapPos;
    #endif
}

// OccupancyBitmaskVoxelレイトレース.
float4 trace_ray_vs_obm_voxel_grid(
    out int out_hit_voxel_index,

    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 grid_toroidal_offset, Buffer<uint> occupancy_bitmask_voxel)
{
    const float3 grid_box_min = float3(0.0, 0.0, 0.0);
    const float3 grid_box_max = float3(grid_resolution);
    const int3 grid_box_cell_max = int3(grid_resolution - 1);
    const float cell_width_ws_inv = 1.0 / cell_width_ws;

    float3 ray_pos = (ray_origin_ws - grid_min_ws) * cell_width_ws_inv;

    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    float3 inv_dir;
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, inv_dir, grid_box_min, grid_box_max, ray_pos, (ray_pos + ray_dir_ws * (trace_distance_ws * cell_width_ws_inv))))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);// ヒット無し.
    }

    const int3 raySign = sign(ray_dir_ws);
    const float3 startPos = floor(ray_pos + ray_dir_ws * ray_trace_begin_t_offset);
    float3 sideDist = ((startPos - ray_pos) + step(0.0, ray_dir_ws)) * inv_dir;
    int3 mapPos = int3(startPos);
    bool3 stepMask = calc_dda_trace_step_mask(sideDist);

    float hit_t = -1.0;
    for(;;)
    {
        // 読み取り用のマッピングをして読み取り.
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, grid_toroidal_offset, grid_resolution), grid_resolution);
        const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
        
        // CoarseVoxelで簡易判定.
        if(0 != (occupancy_bitmask_voxel[unique_data_addr]))
        {
            // TODO. WaveActiveCountBitsを使用して一定数以上(８割以上~等)のLaneが到達するまでcontinueすることで発散対策をすることも検討.
            const float3 mini = ((mapPos-ray_pos) + 0.5 - 0.5*float3(raySign)) * inv_dir;
            const float d = max(mini.x, max(mini.y, mini.z));
            const float3 intersect = ray_pos + ray_dir_ws*d;
            float3 uv3d = intersect - mapPos;

            // レイ始点がBrick内部に入っている場合の対応.
            if (all(mapPos == floor(ray_pos)))
                uv3d = ray_pos - mapPos;

            const int3 subp = trace_bitmask_brick(uv3d*k_obm_per_voxel_resolution, ray_dir_ws, inv_dir, stepMask, occupancy_bitmask_voxel, voxel_index);
            // obm bitmaskヒット.
            if (subp.x >= 0) 
            {
                const float3 finalPos = mapPos*k_obm_per_voxel_resolution+subp;
                const float3 startPos = ray_pos*k_obm_per_voxel_resolution;
                const float3 mini = ((finalPos-startPos) + 0.5 - 0.5*float3(raySign)) * inv_dir;
                const float d = max(mini.x, max(mini.y, mini.z));
                hit_t = d/k_obm_per_voxel_resolution;
                break;
            }
        }

        stepMask = calc_dda_trace_step_mask(sideDist);
        sideDist += select(stepMask, abs(inv_dir), 0);
        const int3 mapPosDelta = select(stepMask, raySign, 0);
        // ここがゼロになって無限ループになる場合があるため安全break.
        if(all(mapPosDelta == 0)) {break;}
        mapPos += mapPosDelta;

        // 範囲外.
        if(!check_grid_bound(mapPos, grid_resolution.x, grid_resolution.y, grid_resolution.z)) {break;}
    }
    if(0.0 <= hit_t)
    {
        out_hit_voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, grid_toroidal_offset, grid_resolution), grid_resolution);
        const float3 hit_normal = select(stepMask, -sign(ray_dir_ws), 0.0);
        const float hit_t_ws = (hit_t + ray_trace_begin_t_offset) * cell_width_ws;
        return float4(hit_t_ws, hit_normal.x, hit_normal.y, hit_normal.z);
    }
    return float4(-1.0, -1.0, -1.0, -1.0);
}










//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 旧バージョンレイトレース
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Cell移動の精密化処理(オプション).
// 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先). この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
int3 FineCellStep(int3 cell_step)
{
    int tmp = cell_step.x;
    cell_step.y = (0 < tmp) ? 0 : cell_step.y;
    tmp += cell_step.y;
    cell_step.z = (0 < tmp) ? 0 : cell_step.z;
    return cell_step;
}
// 第一象限(すべて正)のDDA中の現在のトレース時刻から次のセル移動を計算.
int3 calc_trace_cell_step_dir_abs(float3 curr_trace_time_abs)
{
    // 最小コンポーネント方向へ移動する.
    return select(curr_trace_time_abs <= min(curr_trace_time_abs.yzx, curr_trace_time_abs.zxy), int3(1,1,1), int3(0,0,0));
}
int3 calc_trace_cell_step_dir_abs_fine(float3 curr_trace_time_abs)
{
    // 最小コンポーネントが複数ある場合に一つに制限する精密版.
    return FineCellStep(calc_trace_cell_step_dir_abs(curr_trace_time_abs));
}

// 詳細トレース実行. 粗いトレースとほぼ同じコードが二重化しており無駄が多いので整理対象. 粗いVoxel境界で不正ヒットのノイズが若干ある不具合も存在.
//  return : [0.0, world_space_t) (ヒット無しの場合は負数).
//  voxel_index : 内部ビットセルまで参照する精密トレースを行う場合にそのVoxelIndexを指定..
float4 trace_ray_vs_occupancy_bitmask_voxel_fine(
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    Buffer<uint> occupancy_bitmask_voxel, uint voxel_index
    ,int3 parent_trace_cell_id
    )
{
    const float3 grid_box_min = float3(0.0, 0.0, 0.0);
    const float3 grid_box_max = float3(grid_resolution);
    const int3 grid_box_cell_max = int3(grid_resolution - 1);

    // 後段のDDAのためにCell単位空間の始点終点に変換.
    const float3 ray_origin_grid = (ray_origin_ws - grid_min_ws) * (1.0 / cell_width_ws);
    const float3 ray_end_grid = ((ray_origin_ws + ray_dir_ws * trace_distance_ws) - grid_min_ws) * (1.0 / cell_width_ws);

    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    float3 ray_dir_inv_grid;
    // レイセットアップ.
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, ray_dir_inv_grid, grid_box_min, grid_box_max, ray_origin_grid, ray_end_grid))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);// ヒット無し.
    }

    const uint voxel_obm_addr = obm_voxel_occupancy_bitmask_data_addr(voxel_index);

    const float ray_trace_len = ray_trace_end_t_offset - ray_trace_begin_t_offset;// トレース範囲の距離.
    const float3 dir_sign = sign(ray_dir_ws);// +1 : positive component or zero, -1 : negative component.
    const float3 trace_time_base = dir_sign * ray_dir_inv_grid;

    // Grid内にクランプしたトレース始点終点.
    const float3 ray_trace_begin_grid = ray_origin_grid + ray_trace_begin_t_offset * ray_dir_ws;
    const float3 ray_trace_end_grid = ray_origin_grid + ray_trace_end_t_offset * ray_dir_ws;

    // 0ベースでのトラバースCell範囲.
    const int3 trace_begin_cell = min(floor(ray_trace_begin_grid), grid_box_cell_max);
    const int3 trace_end_cell = min(floor(ray_trace_end_grid), grid_box_cell_max);
    const int3 trance_cell_step_max = abs(trace_end_cell - trace_begin_cell);

    // 始点からの最初のステップt.
    const float3 trace_time_start_offset = abs((floor(ray_trace_begin_grid) + max(dir_sign, float3(0,0,0)) - ray_trace_begin_grid) * ray_dir_inv_grid);

    // デバッグ.
    float3 fine_trace_optional_return = float3(0,0,0);
    int debug_step_count = 0;

    float curr_ray_t = 1e20;//ray_trace_len;// 初期値は最大長.
    int3 total_cell_step = int3(0,0,0);
    float3 trace_time_total = float3(0,0,0);
    int3 prev_cell_step = int3(0,0,0);
    int3 curr_cell_step = calc_principal_axis(abs(dir_sign));//int3(0,0,0);// 初期ステップ方向. トレースには使われないが, 初回でヒットした場合の法線方向検出のため設定.
    // DDAのトラバースであるため, 有効ヒットがあれば即座に最近接として終了.
    for (;ray_trace_len <= curr_ray_t;)
    {
        // 到達Cell
        const int3 trace_cell_id = trace_begin_cell + int3(dir_sign) * total_cell_step;
        uint bitmask_u32_offset, bitmask_u32_bit_pos;
        calc_occupancy_bitmask_voxel_inner_bit_info(bitmask_u32_offset, bitmask_u32_bit_pos, trace_cell_id);

        bool is_hit = occupancy_bitmask_voxel[voxel_obm_addr + bitmask_u32_offset] & (1 << bitmask_u32_bit_pos);

        if(is_hit)
        {
            const float prev_ray_t = curr_ray_t;
            const float trace_time_step = min(trace_time_total.x, min(trace_time_total.y, trace_time_total.z));
            curr_ray_t = min(curr_ray_t, trace_time_step);
            fine_trace_optional_return = trace_time_total;//float3(curr_ray_t, trace_time_step, curr_ray_t);
        }
     
        // 次のCellへ移動.
        {
            prev_cell_step = curr_cell_step;

            trace_time_total = trace_time_start_offset + float3(total_cell_step) * trace_time_base;// 整数セルで進行しながら誤差を回避しつつ毎回総移動量ベクトルを計算.
            curr_cell_step = calc_trace_cell_step_dir_abs(trace_time_total);
            //#if ENABLE_FINE_CELL_STEP_TRACE
            //    curr_cell_step = calc_trace_cell_step_dir_abs_fine(curr_cell_step);
            //#endif
            // セル移動.
            total_cell_step += curr_cell_step;
            if(any(trance_cell_step_max < total_cell_step))
            {
                break;
            }
        }
    }
    
    const float3 hit_normal = prev_cell_step * dir_sign;
    const float t_ws = (curr_ray_t + ray_trace_begin_t_offset) * cell_width_ws;
    const float ret_t = (ray_trace_len > curr_ray_t) ? t_ws : -1.0;
    const float3 optional_return = fine_trace_optional_return;
    return float4(ret_t, ray_trace_len, curr_ray_t, ret_t);
}


// トレース実行.
//  return : [0.0, world_space_t) (ヒット無しの場合は負数).
float4 trace_ray_vs_occupancy_bitmask_voxel(
    out int out_hit_voxel_index,

    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 grid_toroidal_offset, Buffer<uint> occupancy_bitmask_voxel)
{
    const float3 grid_box_min = float3(0.0, 0.0, 0.0);
    const float3 grid_box_max = float3(grid_resolution);
    const int3 grid_box_cell_max = int3(grid_resolution - 1);

    // 後段のDDAのためにCell単位空間の始点終点に変換.
    const float3 ray_origin_grid = (ray_origin_ws - grid_min_ws) * (1.0 / cell_width_ws);
    const float3 ray_end_grid = ((ray_origin_ws + ray_dir_ws * trace_distance_ws) - grid_min_ws) * (1.0 / cell_width_ws);

    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    float3 ray_dir_inv_grid;
    // レイセットアップ.
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, ray_dir_inv_grid, grid_box_min, grid_box_max, ray_origin_grid, ray_end_grid))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);// ヒット無し.
    }

    const float ray_trace_len = ray_trace_end_t_offset - ray_trace_begin_t_offset;// トレース範囲の距離.
    const float3 dir_sign = sign(ray_dir_ws);// +1 : positive component or zero, -1 : negative component.
    const float3 trace_time_base = dir_sign * ray_dir_inv_grid;// すべてが正の第一象限での基準トレースt.

    // Grid内にクランプしたトレース始点終点.
    const float3 ray_trace_begin_grid = ray_origin_grid + ray_trace_begin_t_offset * ray_dir_ws;
    const float3 ray_trace_end_grid = ray_origin_grid + ray_trace_end_t_offset * ray_dir_ws;

    // 0ベースでのトラバースCell範囲.
    const int3 trace_begin_cell = min(floor(ray_trace_begin_grid), grid_box_cell_max);
    const int3 trace_end_cell = min(floor(ray_trace_end_grid), grid_box_cell_max);
    const int3 trance_cell_step_max = abs(trace_end_cell - trace_begin_cell);

    // 始点からの最初のステップt.
    const float3 trace_time_start_offset = abs((floor(ray_trace_begin_grid) + max(dir_sign, float3(0,0,0)) - ray_trace_begin_grid) * ray_dir_inv_grid);

    // デバッグ
    float3 fine_trace_optional_return = float3(0,0,0);
    int debug_step_count = 0;

    float curr_ray_t = 1e20;//ray_trace_len;// 初期値は最大長.
    int3 total_cell_step = int3(0,0,0);
    float3 trace_time_total = float3(0,0,0);
    int3 prev_cell_step = int3(0,0,0);
    int3 curr_cell_step = calc_principal_axis(abs(dir_sign));//int3(0,0,0);// 初期ステップ方向. トレースには使われないが, 初回でヒットした場合の法線方向検出のため設定.
    // DDAのトラバースであるため, 有効ヒットがあれば即座に最近接として終了.
    for (;ray_trace_len <= curr_ray_t;)
    {
        // 到達Cell
        const int3 trace_cell_id = trace_begin_cell + int3(dir_sign) * total_cell_step;
        // 読み取り用のマッピングをして読み取り.
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(trace_cell_id, grid_toroidal_offset, grid_resolution), grid_resolution);

        // CoarseVoxelで簡易判定.
        const uint unique_data_addr = obm_voxel_unique_data_addr(voxel_index);
        if(0 != (occupancy_bitmask_voxel[unique_data_addr]))
        {
            // CoarseVoxelが有効ならば詳細トレース.
            #if 1
                // 占有ビットマスクまで参照する精密トレース.
                const float detail_cell_size = k_obm_per_voxel_resolution_inv;
                const float k_fine_step_start_t_offset = 1e-5;// 内部セルの境界での誤差回避のために微小値を加算.

                // 最小コンポーネントからt値を取得.
                const float trace_time_step = min(trace_time_total.x, min(trace_time_total.y, trace_time_total.z));
                const float3 cur_pos_grid = ray_dir_ws * (trace_time_step + k_fine_step_start_t_offset) + ray_trace_begin_grid;
                const float3 cur_voxel_min = float3(trace_cell_id);
                // 処理対象Voxelのアドレスを指定して詳細トレース呼び出し. ネスト深すぎるため粗いVoxelでのスキップなども含めてもっと浅くする予定.
                const float4 fine_t = trace_ray_vs_occupancy_bitmask_voxel_fine(
                    cur_pos_grid, ray_dir_ws, ray_trace_len - trace_time_step,
                    cur_voxel_min, detail_cell_size, int3(k_obm_per_voxel_resolution, k_obm_per_voxel_resolution, k_obm_per_voxel_resolution),
                    occupancy_bitmask_voxel, voxel_index
                    ,trace_cell_id
                );
                if(0.0 <= fine_t.x)
                {
                    curr_ray_t = min(curr_ray_t, trace_time_step + fine_t.x);// ヒット.
                    curr_cell_step = fine_t.yzw;// 法線.

                    fine_trace_optional_return = float3(voxel_index, 0, 0);// デバッグ用.
                    
                    out_hit_voxel_index = voxel_index;// ヒットVoxelIndexを返却.
                }
            #else
                // CoarseVoxelでヒットしているならそのままヒット扱い.
                const float trace_time_step = min(trace_time_total.x, min(trace_time_total.y, trace_time_total.z));
                curr_ray_t = min(curr_ray_t, trace_time_step);
                curr_cell_step = prev_cell_step;// 法線.
                fine_trace_optional_return = float3(voxel_index, 0, 0);// デバッグ用.

                out_hit_voxel_index = voxel_index;// ヒットVoxelIndexを返却.
            #endif
        }

        // 次のCellへ移動.
        {
            prev_cell_step = curr_cell_step;

            trace_time_total = trace_time_start_offset + float3(total_cell_step) * trace_time_base;// 整数セルで進行しながら誤差を回避しつつ毎回総移動量ベクトルを計算.
            curr_cell_step = calc_trace_cell_step_dir_abs(trace_time_total);
            //#if ENABLE_FINE_CELL_STEP_TRACE
            //    curr_cell_step = calc_trace_cell_step_dir_abs_fine(trace_time_total);
            //#endif
            // セル移動.
            total_cell_step += curr_cell_step;

            if(any(trance_cell_step_max < total_cell_step))
                break;
        }
        
        ++debug_step_count;
    }
    // ヒットがあればワールド空間のt値, なければヒット無しとして負数-1.0を返す.
    const float ret_t = (ray_trace_len > curr_ray_t) ? (curr_ray_t + ray_trace_begin_t_offset) * cell_width_ws : -1.0;
    const float3 hit_normal = normalize(prev_cell_step * dir_sign);
    //const float3 optional_return = hit_normal;
    const float3 optional_return = fine_trace_optional_return;
    return float4(ret_t, optional_return.x, optional_return.y, optional_return.z);
}

#endif // NGL_SHADER_SSVG_UTIL_H
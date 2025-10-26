#ifndef NGL_SHADER_SSVG_UTIL_H
#define NGL_SHADER_SSVG_UTIL_H

#if 0

ssvg_util.hlsli

#endif


#include "../include/math_util.hlsli"
#include "../include/bit_util.hlsli"

// cpp/hlsl共通定義用ヘッダ.
#include "ssvg_common_header.hlsli"


// Probe更新系のCS ThreadGroupSize. Indirectのため共有ヘッダに定義.
// SharedMemのサイズ制限のため調整.
#define PROBE_UPDATE_THREAD_GROUP_SIZE 96





// ------------------------------------------------------------------------------------------------------------------------
// BitmaskBrickVoxel. bbv.
// uint UniqueData + uint[brick数分] OccupancyBitMask.
// UniqueData : 0bit:空ではないなら1, 1-31: 最後に可視状態になったフレーム番号.
Buffer<uint>		BitmaskBrickVoxel;
RWBuffer<uint>		RWBitmaskBrickVoxel;

// BitmaskBrickVoxel毎の追加データ.
StructuredBuffer<BbvOptionalData>		BitmaskBrickVoxelOptionData;
RWStructuredBuffer<BbvOptionalData>	RWBitmaskBrickVoxelOptionData;

Texture2D       		TexProbeSkyVisibility;
RWTexture2D<float>		RWTexProbeSkyVisibility;

Buffer<uint>		VisibleVoxelList;
RWBuffer<uint>		RWVisibleVoxelList;


Buffer<float>		UpdateProbeWork;
RWBuffer<float>		RWUpdateProbeWork;



// World Cache Probe.
StructuredBuffer<WcpProbeData>		WcpProbeBuffer;
RWStructuredBuffer<WcpProbeData>	RWWcpProbeBuffer;


ConstantBuffer<SsvgParam> cb_ssvg;
// ------------------------------------------------------------------------------------------------------------------------


#if 1
    // シンプルなインデックスフラット化.

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
#else
    // Z-Order Morton Codeによるインデックスフラット化. インデックスの局所化によるキャッシュ効率向上を意図.

    // Voxel座標からVoxelIndex計算.
    uint voxel_coord_to_index(int3 coord, int3 resolution)
    {
        return EncodeMortonCodeX10Y10Z10(coord);
    }
    // VoxelIndexからVoxel座標計算.
    int3 index_to_voxel_coord(uint index, int3 resolution)
    {
        return DecodeMortonCodeX10Y10Z10(index);
    }
#endif


// リニアなVoxel座標をループするToroidalマッピングに変換する.
//  ToroidalMapping座標をリニア座標に戻す変換は
//      voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_ssvg.bbv.grid_resolution - cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution)
//  という使い方で可能.
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution)
{
    return (voxel_coord + toroidal_offset) % resolution;
}

// BitmaskBrickVoxelの取り扱い.
// ------------------------------------------------------------------------------------------------------------------------
// VoxelIndexからアドレス計算. Buffer上の該当Voxelデータの先頭アドレスを返す.
uint bbv_voxel_index_to_addr(uint voxel_index)
{
    return voxel_index * k_bbv_per_voxel_u32_count;
}
// Voxel毎のデータ部の固有データ先頭アドレス計算.
uint bbv_voxel_unique_data_addr(uint voxel_index)
{
    return bbv_voxel_index_to_addr(voxel_index) + 0;
}
// Voxel毎のデータ部の占有ビットマスクデータ先頭アドレス計算.
uint bbv_voxel_bitmask_data_addr(uint voxel_index)
{
    // Voxel毎のデータ部の先頭はVoxel固有データ, 占有ビットマスク の順にレイアウト.
    return bbv_voxel_index_to_addr(voxel_index) + k_bbv_common_data_u32_count;
}
// Voxel毎の占有ビットマスクのu32単位数.
uint bbv_voxel_bitmask_uint_count()
{
    return k_bbv_per_voxel_bitmask_u32_count;
}

// BitmaskBrickVoxelの内部座標を元にリニアインデックスを計算.
uint calc_bbv_bitcell_index(uint3 bitcell_pos)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    return bitcell_pos.x + (bitcell_pos.y * k_bbv_per_voxel_resolution) + (bitcell_pos.z * (k_bbv_per_voxel_resolution * k_bbv_per_voxel_resolution));
}
// calc_bbv_bitcell_index で計算したリニアインデックスからVoxelブロック内のオフセットと読み取りビット位置を計算.
void calc_bbv_bitcell_info_from_bitcell_index(out uint out_u32_offset, out uint out_bit_location, uint bitcell_index)
{
    out_u32_offset = bitcell_index / 32;// 何番目のuintか.
    out_bit_location = bitcell_index - (out_u32_offset * 32);// uint内の何番目のビットか.
}
// BitmaskBrickVoxelの内部座標を元にバッファの該当Voxelブロック内のオフセットと読み取りビット位置を計算.
void calc_bbv_bitcell_info(out uint out_u32_offset, out uint out_bit_location, uint3 bitcell_pos)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint bitcell_index = calc_bbv_bitcell_index(bitcell_pos);

    calc_bbv_bitcell_info_from_bitcell_index(out_u32_offset, out_bit_location, bitcell_index);
}

// BitmaskBrickVoxelのビットセルインデックスから k_bbv_per_voxel_resolution^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_bbv_per_voxel_bitmask_bit_count-1
uint3 calc_bbv_bitcell_pos_from_bit_index(uint bit_index)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint3 bit_pos = uint3(bit_index % k_bbv_per_voxel_resolution, (bit_index / k_bbv_per_voxel_resolution) % k_bbv_per_voxel_resolution, bit_index / (k_bbv_per_voxel_resolution * k_bbv_per_voxel_resolution));
    return bit_pos;
}


// ------------------------------------------------------------------------------------------------------------------------

// Bbvのユニークデータレイアウトメモ.
struct BbvVoxelUniqueData
{
    uint is_occupied;
    uint last_visible_frame;
};
// 0 : からで無いなら1
// 1-8 : 最後に可視状態になったフレーム番号. 0-255でループ.

// ユニークデータに埋め込むためのフレーム番号マスク処理.
uint mask_bbv_voxel_unique_data_last_visible_frame(uint last_visible_frame)
{
    return (last_visible_frame & 0xff);
}
// BitmaskBrickVoxelのユニークデータの構築.
uint build_bbv_voxel_unique_data(BbvVoxelUniqueData data)
{
    // BitmaskBrickVoxelのユニークデータレイアウトメモに則ってuintエンコード.
    return (data.is_occupied & 0x1) | (mask_bbv_voxel_unique_data_last_visible_frame(data.last_visible_frame) << 1);
}
// BitmaskBrickVoxelのユニークデータを展開.
void parse_bbv_voxel_unique_data(out BbvVoxelUniqueData out_data, uint unique_data)
{
    // BitmaskBrickVoxelのユニークデータレイアウトメモに則ってuintからでコード.
    out_data.is_occupied = (unique_data >> 0) & 0x1;
    out_data.last_visible_frame = (unique_data >> 1) & 0xff;
}

// ------------------------------------------------------------------------------------------------------------------------
// BitmaskBrickVoxelの追加データ構造の操作.

//  probe_bitcell_index : -1なら空セル無し, 0〜k_bbv_per_voxel_bitmask_bit_count-1
void set_bbv_probe_bitcell_index(inout BbvOptionalData voxel_data, int probe_bitcell_index)
{
     // 0は空セル無しのフラグとして予約.
    voxel_data.probe_pos_code = (0 <= probe_bitcell_index)? probe_bitcell_index + 1 : 0;
}
// Occupancy Bitmask Voxelのビットセルインデックスからk_bbv_per_voxel_resolution^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_bbv_per_voxel_bitmask_bit_count-1
int calc_bbv_probe_bitcell_index(BbvOptionalData voxel_data)
{
    return voxel_data.probe_pos_code-1;
}


// ------------------------------------------------------------------------------------------------------------------------
// Voxelデータクリア.
void clear_voxel_data(RWBuffer<uint> bbv_buffer, uint voxel_index)
{
    const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
    // 固有データクリア.
    for(int i = 0; i < k_bbv_common_data_u32_count; ++i)
    {
        bbv_buffer[unique_data_addr + i] = 0;
    }

    // 占有ビットマスククリア.
    const uint bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
    for(int i = 0; i < bbv_voxel_bitmask_uint_count(); ++i)
    {
        bbv_buffer[bbv_addr + i] = 0;
    }
}


// ------------------------------------------------------------------------------------------------------------------------
// ワールド座標からBbvの値を読み取る.
uint read_bbv_voxel_from_world_pos(Buffer<uint> bbv_buffer, int3 grid_resolution, int3 bbv_grid_toroidal_offset, float3 grid_min_pos_world, float bbv_cell_size_inv, float3 pos_world)
{
    // WorldPosからVoxelCoordを計算.
    const float3 voxel_coordf = (pos_world - grid_min_pos_world) * bbv_cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < grid_resolution))
    {
        int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, bbv_grid_toroidal_offset, grid_resolution);
        uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, grid_resolution);

        const uint voxel_bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
        // 占有ビットマスクの座標.
        const float3 voxel_coord_frac = frac(voxel_coordf);
        const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_bbv_per_voxel_resolution);
        // 占有ビットマスクのデータ部情報.
        uint bitcell_u32_offset;
        uint bitcell_u32_bit_pos;
        calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);
        const uint bitmask_append = (1 << bitcell_u32_bit_pos);
        // 読み取り.
        return (bbv_buffer[voxel_bbv_addr + bitcell_u32_offset] & bitmask_append) ? 1 : 0;
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

// BitmaskBrickVoxel内部のビットセル単位でのレイトレース.
// https://github.com/dubiousconst282/VoxelRT
int3 trace_bitmask_brick(float3 rayPos, float3 rayDir, float3 invDir, inout bool3 stepMask, 
        Buffer<uint> bbv_buffer, uint voxel_index) 
{
    rayPos = clamp(rayPos, 0.0001, float(k_bbv_per_voxel_resolution)-0.0001);

    float3 sideDist = ((floor(rayPos) - rayPos) + step(0.0, rayDir)) * invDir;
    int3 mapPos = int3(floor(rayPos));

    int3 raySign = sign(rayDir);
    #if 1
        const uint bbv_bitmask_addr = bbv_voxel_bitmask_data_addr(voxel_index);
        do {
            uint bitcell_u32_offset, bitcell_u32_bit_pos;
            calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, mapPos);
            bool is_hit = bbv_buffer[bbv_bitmask_addr + bitcell_u32_offset] & (1 << bitcell_u32_bit_pos);

            if(is_hit) { return mapPos; }

            stepMask = calc_dda_trace_step_mask(sideDist);
            const int3 mapPosDelta = select(stepMask, raySign, 0);
            if(all(mapPosDelta == 0)) {break;}// ここがゼロになって無限ループになる場合がある???  ため安全break.
            
            mapPos += mapPosDelta;
            sideDist += select(stepMask, abs(invDir), 0);
        } while (all(uint3(mapPos) < k_bbv_per_voxel_resolution));

        return -1;
    #else
        // デバッグ用に即時ヒット扱い.
        return mapPos;
    #endif
}

// BitmaskBrickVoxelレイトレース.
float4 trace_ray_vs_bitmask_brick_voxel_grid(
    out int out_hit_voxel_index,

    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer)
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
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, bbv_grid_toroidal_offset, grid_resolution), grid_resolution);
        const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
        
        // Voxelで簡易判定.
        BbvVoxelUniqueData unique_data;
        parse_bbv_voxel_unique_data(unique_data, bbv_buffer[unique_data_addr]);
        if(0 != (unique_data.is_occupied))
        {
            // TODO. WaveActiveCountBitsを使用して一定数以上(８割以上~等)のLaneが到達するまでcontinueすることで発散対策をすることも検討.
            const float3 mini = ((mapPos-ray_pos) + 0.5 - 0.5*float3(raySign)) * inv_dir;
            const float d = max(mini.x, max(mini.y, mini.z));
            const float3 intersect = ray_pos + ray_dir_ws*d;

            // レイ始点がBrick内部に入っている場合のエッジケース対応.
            const bool is_ray_origin_inner_voxel = all(mapPos == floor(ray_pos));
            const float3 uv3d = is_ray_origin_inner_voxel ? ray_pos - mapPos : intersect - mapPos;

            const int3 subp = trace_bitmask_brick(uv3d*k_bbv_per_voxel_resolution, ray_dir_ws, inv_dir, stepMask, bbv_buffer, voxel_index);
            // bitmaskヒット.
            if (subp.x >= 0) 
            {
                const float3 finalPos = mapPos*k_bbv_per_voxel_resolution+subp;
                const float3 startPos = ray_pos*k_bbv_per_voxel_resolution;
                const float3 mini = ((finalPos-startPos) + 0.5 - 0.5*float3(raySign)) * inv_dir;
                const float d = max(mini.x, max(mini.y, mini.z));
                hit_t = d/k_bbv_per_voxel_resolution;
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
        out_hit_voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, bbv_grid_toroidal_offset, grid_resolution), grid_resolution);
        const float3 hit_normal = select(stepMask, -sign(ray_dir_ws), 0.0);
        const float hit_t_ws = (hit_t + ray_trace_begin_t_offset) * cell_width_ws;
        return float4(hit_t_ws, hit_normal.x, hit_normal.y, hit_normal.z);
    }
    return float4(-1.0, -1.0, -1.0, -1.0);
}


#endif // NGL_SHADER_SSVG_UTIL_H
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

// ジオメトリ可視表面のBbvリスト. 0番要素はカウンタ.
Buffer<uint>		VisibleVoxelList;
RWBuffer<uint>		RWVisibleVoxelList;

// ジオメトリ表面ではなくなった除去Bbvリスト. 0番要素xはカウンタ. 
// 詳細情報を含める必要があるためuint*4相当のバッファ. カウンタ用Atomic操作するためバッファ型としてはScalar型にしている.
// k_component_count_RemoveVoxelList単位. i番目のデータは (1+i)*k_component_count_RemoveVoxelList から k_component_count_RemoveVoxelList個.
Buffer<uint>		RemoveVoxelList;
RWBuffer<uint>		RWRemoveVoxelList;

// RemoveVoxelのデバッグ用情報を格納するリスト. RemoveVoxelListと同じ要素数と運用.
RWBuffer<float>		RWRemoveVoxelDebugList;


Buffer<float>		UpdateProbeWork;
RWBuffer<float>		RWUpdateProbeWork;

// World Cache Probe.
StructuredBuffer<WcpProbeData>		WcpProbeBuffer;
RWStructuredBuffer<WcpProbeData>	RWWcpProbeBuffer;

Texture2D       		WcpProbeAtlasTex;
RWTexture2D<float>		RWWcpProbeAtlasTex;

// 0番目はアトミックカウンタ, それ以降をリスト利用.
Buffer<uint>		SurfaceProbeCellList;
RWBuffer<uint>		RWSurfaceProbeCellList;

// ssvgのメインパラメータ.
ConstantBuffer<SsvgParam> cb_ssvg;

// ------------------------------------------------------------------------------------------------------------------------


#if 0
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
// Voxel毎のCoarseOccupancyビットマスクのアドレス計算. Voxelのu32要素毎の非ゼロ状態を集約した粗いビットマスク情報.
uint bbv_voxel_coarse_occupancy_info_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 0;// ユニーク部の先頭に配置.
}
// Voxel毎の作業用データ部アドレス.
uint bbv_voxel_brick_work_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 1;// ユニーク部の２つ目に配置.
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
// Bbvのユニークデータレイアウト.

// uint[0]      :  brickの各u32コンポーネントそれぞれの非ゼロフラグ集約ビット. ここだけ独立してAtomic操作をしたいためuintコンポーネントを分けた.
// uint[1].8bit : 最後に可視状態になったフレーム番号. 0-255でループ.

// ユニークデータに埋め込むためのフレーム番号マスク処理.
uint mask_bbv_voxel_unique_data_last_visible_frame(uint last_visible_frame)
{
    return (last_visible_frame & 0xff);
}

// ------------------------------------------------------------------------------------------------------------------------
// BitmaskBrickVoxelの追加データ構造の操作.

// Bbv. probe_bitcell_index : -1なら空セル無し, 0〜k_bbv_per_voxel_bitmask_bit_count-1
void set_bbv_probe_bitcell_index(inout BbvOptionalData voxel_data, int probe_bitcell_index)
{
     // 0は空セル無しのフラグとして予約.
    voxel_data.probe_pos_code = (0 <= probe_bitcell_index)? probe_bitcell_index + 1 : 0;
}
// Bbv. Bitmask Brick Voxelのビットセルインデックスからk_bbv_per_voxel_resolution^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_bbv_per_voxel_bitmask_bit_count-1
int calc_bbv_probe_bitcell_index(BbvOptionalData voxel_data)
{
    return voxel_data.probe_pos_code-1;
}


// ------------------------------------------------------------------------------------------------------------------------
// Bbv. Voxelデータクリア.
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
// Bbv. ワールド座標から占有値を読み取る.
uint read_bbv_voxel_from_world_pos(Buffer<uint> bbv_buffer, int3 grid_resolution, int3 bbv_grid_toroidal_offset, float3 grid_min_pos_world, float bbv_cell_size_inv, float3 pos_world)
{
    // WorldPosからVoxelCoordを計算.
    const float3 voxel_coordf = (pos_world - grid_min_pos_world) * bbv_cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < grid_resolution))
    {
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, bbv_grid_toroidal_offset, grid_resolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, grid_resolution);

        const uint voxel_bbv_addr = bbv_voxel_bitmask_data_addr(voxel_index);
        // 占有ビットマスクの座標.
        const float3 voxel_coord_frac = frac(voxel_coordf);
        const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * k_bbv_per_voxel_resolution);
        // 占有ビットマスクのデータ部情報.
        uint bitcell_u32_offset;
        uint bitcell_u32_bit_pos;
        calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);
        const uint bitmask_append = (1u << bitcell_u32_bit_pos);
        // 読み取り.
        return (bbv_buffer[voxel_bbv_addr + bitcell_u32_offset] & bitmask_append) ? 1 : 0;
    }

    return 0;
}


//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Bbvレイキャスト.
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 呼び出し側で引数の符号なし uint3 pos に符号付きint3を渡すことでオーバーフローして最大値になるため, 0 <= pos < size の範囲内にあるかをチェックとなる.
bool check_grid_bound(uint3 pos, uint sizeX, uint sizeY, uint sizeZ) {
    return pos.x < sizeX && pos.y < sizeY && pos.z < sizeZ;
}
// ray dir の逆数による次の境界への距離からdda用のステップ用の最小距離の軸選択boolマスクを計算.
bool3 calc_dda_trace_step_mask(float3 ray_side_distance) {
    bool3 mask;
    mask.x = ray_side_distance.x < ray_side_distance.y && ray_side_distance.x < ray_side_distance.z;
    mask.y = !mask.x && ray_side_distance.y < ray_side_distance.z;
    mask.z = !mask.x && !mask.y;
    return mask;
}

// BitmaskBrickVoxel内部のビットセル単位でのレイトレース.
// https://github.com/dubiousconst282/VoxelRT
int3 trace_bitmask_brick(float3 rayPos, float3 rayDir, float3 rayDirSign, float3 invDir, inout bool3 stepMask, 
        Buffer<uint> bbv_buffer, uint voxel_index,
        const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
    ) 
{
    rayPos = clamp(rayPos, 0.0001, float(k_bbv_per_voxel_resolution)-0.0001);

    float3 sideDist = ((floor(rayPos) - rayPos) + step(0.0, rayDir)) * invDir;
    int3 mapPos = int3(floor(rayPos));

    int3 raySign = rayDirSign;
    if(!is_brick_mode)
    {
        const uint bbv_bitmask_addr = bbv_voxel_bitmask_data_addr(voxel_index);
        do {
            uint bitcell_u32_offset, bitcell_u32_bit_pos;
            calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, mapPos);
            bool is_hit = bbv_buffer[bbv_bitmask_addr + bitcell_u32_offset] & (1u << bitcell_u32_bit_pos);

            if(is_hit) { return mapPos; }

            stepMask = calc_dda_trace_step_mask(sideDist);
            const int3 mapPosDelta = select(stepMask, raySign, 0);
            if(all(mapPosDelta == 0)) {break;}// ここがゼロになって無限ループになる場合がある???  ため安全break.
            
            mapPos += mapPosDelta;
            sideDist += select(stepMask, abs(invDir), 0);
        } while (all(uint3(mapPos) < k_bbv_per_voxel_resolution));

        return -1;
    }
    else
    {
        // デバッグ用にBrick単位で即時ヒット扱いする. この関数に入る時点でBrick単位のOccupiedフラグを参照しているはず.
        return mapPos;
    }
}


// レイの始点終点セットアップ. 領域AABBの内部または表面から開始するための始点終点のt値( origin + dir * t) を計算.
// aabb_min, aabb_max, ray_origin, ray_end のすべての空間が一致していればどの空間の情報でも適切な結果を返す(World空間でもCell基準空間でも).
bool calc_ray_t_offset_for_aabb(out float out_aabb_clamped_origin_t, out float out_aabb_clamped_end_t, float3 aabb_min, float3 aabb_max, float3 ray_origin, float3 ray_dir, float3 ray_dir_inv, float ray_len)
{
    out_aabb_clamped_origin_t = 0.0;
    out_aabb_clamped_end_t = ray_len;

    const float3 t_to_min = (aabb_min - ray_origin) * ray_dir_inv;
    const float3 t_to_max = (aabb_max - ray_origin) * ray_dir_inv;
    const float t_near = Max3(min(t_to_min, t_to_max));
    const float t_far = Min3(max(t_to_min, t_to_max));

    // GridBoxとの交点が存在しなければ早期終了. t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
    if (t_far < 0.0 || t_far <= t_near || ray_len < t_near)
        return false;

    // 結果を返す. このt値で origin + dir * t を計算すればそれぞれ始点と終点がAABB空間内にクランプされた座標になる.
    out_aabb_clamped_origin_t = max(out_aabb_clamped_origin_t, t_near);
    out_aabb_clamped_end_t = min(out_aabb_clamped_end_t, t_far);

    return true;
};
// BitmaskBrickVoxelレイトレース. 高速化検証.
float4 trace_bbv_core(
    out int out_hit_voxel_index,
    out float4 out_debug,

    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,

    const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
)
{
    const float3 grid_box_min = float3(0.0, 0.0, 0.0);
    const float3 grid_box_max = float3(grid_resolution);
    const float cell_width_ws_inv = 1.0 / cell_width_ws;

    out_hit_voxel_index = -1;
    out_debug = float4(0.0, 0.0, 0.0, 0.0);// デバッグ用.

    const float3 ray_dir_inv = 1.0 / ray_dir_ws;// inf対策が必要な場合があるかも. -> select( ray_dir_component_nearly_zero, float3(k_float_max, k_float_max, k_float_max), 1.0 / ray_dir_c)
    const float3 ray_dir_sign = sign(ray_dir_ws);// ray_dirの値がゼロのコンポーネントについては 0 が格納される. これは軸並行ベクトルの場合の計算を正しく行うためのマスクとしても機能する.

    const float3 ray_origin = (ray_origin_ws - grid_min_ws) * cell_width_ws_inv;
    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, grid_box_min, grid_box_max, ray_origin, ray_dir_ws, ray_dir_inv, trace_distance_ws * cell_width_ws_inv))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);// ヒット無し.
    }
    const float3 ray_component_validity = abs(ray_dir_sign);
    // ここ以降でray_originを使用していたが, AABB外からのレイの場合に不具合が発生していた. 正しく動作させるためにClampされた始点をray_originの代わりに使うように修正. 
    const float3 clampled_start_pos = ray_origin + ray_dir_ws * ray_trace_begin_t_offset;
    const float3 clampled_start_pos_i = floor(clampled_start_pos);
    

    float3 sideDist = ((clampled_start_pos_i - clampled_start_pos) + step(0.0, ray_dir_ws)) * ray_dir_inv;
    int3 mapPos = int3(clampled_start_pos_i);
    bool3 stepMask = calc_dda_trace_step_mask(sideDist);
    float hit_t = -1.0;
    // デバッグ用.
    int debug_step_count = 0;
    // トレースループ.
    for(;;)
    {
        // 読み取り用のマッピングをして読み取り.
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, bbv_grid_toroidal_offset, grid_resolution), grid_resolution);
        
        // Voxelで簡易判定.
        const uint bbv_occupied_flag = BitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(voxel_index)] & k_bbv_per_voxel_bitmask_u32_component_mask;
        if(0 != (bbv_occupied_flag))
        {
            // signのabsをマスクとして使用することで軸並行レイのエッジケースに対応.
            const float3 mini = ((mapPos-clampled_start_pos) + 0.5*ray_component_validity - 0.5*ray_dir_sign) * ray_dir_inv;
            const float d = Max3(mini);
            const float3 intersect = clampled_start_pos + ray_dir_ws*d;

            // レイ始点がBrick内部に入っている場合のエッジケース対応.
            const bool is_ray_origin_inner_voxel = all(mapPos == floor(clampled_start_pos));
            const float3 uv3d = is_ray_origin_inner_voxel ? clampled_start_pos - mapPos : intersect - mapPos;
            // BitmaskBrickVoxel内部のビットセル単位でレイトレース.
            const int3 subp = trace_bitmask_brick(uv3d*k_bbv_per_voxel_resolution, ray_dir_ws, ray_dir_sign, ray_dir_inv, stepMask, bbv_buffer, voxel_index, is_brick_mode);
            // bitmaskヒット. 未ヒットなら何れかの要素が-1の結果が返ってくる.
            if (subp.x >= 0)
            {
                const float3 finalPos = mapPos*k_bbv_per_voxel_resolution+subp;
                const float3 startPos = clampled_start_pos*k_bbv_per_voxel_resolution;
                // signのabsをマスクとして使用することで軸並行レイのエッジケースに対応.
                const float3 mini = ((finalPos-startPos) + 0.5*ray_component_validity - 0.5*ray_dir_sign) * ray_dir_inv;
                const float d = max(mini.x, max(mini.y, mini.z));
                
                // ヒットしているはずなので0以上とする. ない場合はレイ始点がセル内の場合ヒット無し扱いになり, アーティファクトが発生する.
                hit_t = max(0.0, d * k_bbv_per_voxel_resolution_inv);
                break;
            }
        }

        stepMask = calc_dda_trace_step_mask(sideDist);
        sideDist += select(stepMask, abs(ray_dir_inv), 0);
        const int3 mapPosDelta = select(stepMask, ray_dir_sign, 0);
        // ここがゼロになって無限ループになる場合があるため安全break.
        if(all(mapPosDelta == 0)) {break;}
        mapPos += mapPosDelta;

            ++debug_step_count;

        // 範囲外.
        if(!check_grid_bound(mapPos, grid_resolution.x, grid_resolution.y, grid_resolution.z)) {break;}
    }

    //out_debug.x = debug_step_count;
    out_debug.xyz = clampled_start_pos;
    out_debug.w = ray_trace_begin_t_offset;

    if(0.0 <= hit_t)
    {
        // ヒットセル情報.
        out_hit_voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(mapPos, bbv_grid_toroidal_offset, grid_resolution), grid_resolution);
        // ヒット法線.
        const float3 hit_normal = select(stepMask, -ray_dir_sign, 0.0);
        // ヒットt値(ワールド空間).
        const float hit_t_ws = (hit_t + ray_trace_begin_t_offset) * cell_width_ws;
        return float4(hit_t_ws, hit_normal.x, hit_normal.y, hit_normal.z);
    }
    return float4(-1.0, -1.0, -1.0, -1.0);
}






// BitmaskBrickVoxelレイトレース.
float4 trace_bbv(
    out int out_hit_voxel_index,
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer
)
{
    return trace_bbv_core(
        out_hit_voxel_index,
        out_debug,
        ray_origin_ws, ray_dir_ws, trace_distance_ws,
        grid_min_ws, cell_width_ws, grid_resolution,
        bbv_grid_toroidal_offset, bbv_buffer,
        false
    );
}
// BitmaskBrickVoxelレイトレース. 開発用.
float4 trace_bbv_dev(
    out int out_hit_voxel_index,
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
)
{
    return trace_bbv_core(
        out_hit_voxel_index,
        out_debug,
        ray_origin_ws, ray_dir_ws, trace_distance_ws,
        grid_min_ws, cell_width_ws, grid_resolution,
        bbv_grid_toroidal_offset, bbv_buffer,
        is_brick_mode
    );
}



//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Wcp.
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 符号付き, 要素が-1:+1範囲のベクトルをuintにエンコード.
uint encode_range1_vec3_to_uint(float3 v)
{
    // 3要素の符号を3bitに格納. 負数で1.
    const uint sign3 = (select(v.x < 0.0, 1u, 0u) << 2) | (select(v.y < 0.0, 1u, 0u) << 1) | (select(v.z < 0.0, 1u, 0u) << 0);

    // -1~1のベクトルを9bit固定小数点数に変換.
    v = abs(v);
    const uint x_fixed = (uint)(v.x * 511.0 + 0.5);
    const uint y_fixed = (uint)(v.y * 511.0 + 0.5);
    const uint z_fixed = (uint)(v.z * 511.0 + 0.5); 
    // 符号3bitを最上位に, 9bit固定小数点数を下位に詰め込む.
    return (sign3 << 27) | (x_fixed << 18) | (y_fixed << 9) | (z_fixed << 0);
}
// uintから 要素が-1:+1範囲の3要素ベクトルをデコード.
float3 decode_uint_to_range1_vec3(uint code)
{
    const uint sign3 = (code >> 27) & 0x7;
    const uint x_fixed = (code >> 18) & 0x1ff;
    const uint y_fixed = (code >> 9) & 0x1ff;
    const uint z_fixed = (code >> 0) & 0x1ff;

    float3 v;
    v.x = (float)x_fixed * (1.0 / 511.0);
    v.y = (float)y_fixed * (1.0 / 511.0);
    v.z = (float)z_fixed * (1.0 / 511.0);

    // 符号.
    v *= select(bool3((sign3 & 0x4), (sign3 & 0x2), (sign3 & 0x1)), float3(-1.0, -1.0, -1.0), float3(1.0, 1.0, 1.0));

    return v;
}


#endif // NGL_SHADER_SSVG_UTIL_H
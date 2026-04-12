#ifndef NGL_SHADER_SRVS_UTIL_H
#define NGL_SHADER_SRVS_UTIL_H

#if 0

srvs_util.hlsli

#endif


#include "../include/math_util.hlsli"
#include "../include/rand_util.hlsli"
#include "../include/bit_util.hlsli"

// cpp/hlsl共通定義用ヘッダ.
#include "srvs_common_header.hlsli"


// Probe更新系のCS ThreadGroupSize. Indirectのため共有ヘッダに定義.
// SharedMemのサイズ制限のため調整.
#define PROBE_UPDATE_THREAD_GROUP_SIZE 96





// ------------------------------------------------------------------------------------------------------------------------
// Bbv. bbv.
// 1. Voxel bit data region
// 2. Brick data region
// 3. HiBrick data region
Buffer<uint>		BitmaskBrickVoxel;
RWBuffer<uint>		RWBitmaskBrickVoxel;

// Bbv毎の追加データ.
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


// Screen Space Probe.
Texture2D<float4>      ScreenSpaceProbeTex;
Texture2D<float4>      ScreenSpaceProbeHistoryTex;
RWTexture2D<float4>    RWScreenSpaceProbeTex;

// 1/SCREEN_SPACE_PROBE_INFO_DOWNSCALE resolution Per ScreenSpaceProbe Tile Info Texture.
// r.x : Depth, r.y : Probe位置オフセットのフラットインデックス, r.zw : OctEncodeしたWS法線.
// x が1 の場合は無効深度で, Probeも存在しない.
Texture2D<float4>      ScreenSpaceProbeTileInfoTex;
Texture2D<float4>      ScreenSpaceProbeHistoryTileInfoTex;
RWTexture2D<float4>    RWScreenSpaceProbeTileInfoTex;

// L1 SH (SkyVisibility).
Texture2D<float4>      ScreenSpaceProbeSHTex;
RWTexture2D<float4>    RWScreenSpaceProbeSHTex;

// Persistent side cache for ScreenSpaceProbe.
Texture2D<float4>      ScreenSpaceProbeSideCacheTex;
RWTexture2D<float4>    RWScreenSpaceProbeSideCacheTex;
// xyz: cached probe world position, w: last update frame index.
Texture2D<float4>      ScreenSpaceProbeSideCacheMetaTex;
RWTexture2D<float4>    RWScreenSpaceProbeSideCacheMetaTex;
RWTexture2D<uint>      RWScreenSpaceProbeSideCacheLockTex;

// DirectSH方式専用リソース (OctMapを持たずSHで直接保持する検証パス).
// 1/8解像度のProbeタイル情報 (既存 ScreenSpaceProbeTileInfoTex と同形式).
Texture2D<float4>      ScreenSpaceProbeDirectSHTileInfoTex;
Texture2D<float4>      ScreenSpaceProbeDirectSHHistoryTileInfoTex;
RWTexture2D<float4>    RWScreenSpaceProbeDirectSHTileInfoTex;
// 1/8解像度のL1 SH係数 (rgba = Y00, Y1_{-1}(y), Y1_0(z), Y1_{+1}(x)).
Texture2D<float4>      ScreenSpaceProbeDirectSHTex;
Texture2D<float4>      ScreenSpaceProbeDirectSHHistoryTex;
RWTexture2D<float4>    RWScreenSpaceProbeDirectSHTex;
RWTexture2D<float4>    RWScreenSpaceProbeDirectSHFilteredTex;
// Preupdate で計算した Best Prev Tile (packed uint: upper16=y, lower16=x, 0xffffffff=無効).
Texture2D<uint>        ScreenSpaceProbeDirectSHBestPrevTileTex;
RWTexture2D<uint>      RWScreenSpaceProbeDirectSHBestPrevTileTex;


// srvsのメインパラメータ.
ConstantBuffer<SrvsParam> cb_srvs;



// ------------------------------------------------------------------------------------------------------------------------
bool isValidDepth(float d)
{
    // 深度が有効範囲内かどうかを判定.
    return (0.0 < d && d < 1.0);
}

// ScreenSpaceProbeTileInfo.y のビット割り当て.
// bit0-5: タイル内プローブ位置フラットインデックス(0-63)
// bit6  : Reprojection成功フラグ(1=成功, 0=失敗)
// bit7+ : 予約
static const uint k_ss_probe_tile_info_probe_pos_mask = 0x3fu;
static const uint k_ss_probe_tile_info_reprojection_succeeded_shift = 6u;
static const uint k_ss_probe_tile_info_reprojection_succeeded_mask = (1u << k_ss_probe_tile_info_reprojection_succeeded_shift);

uint SspTileInfoYToPackedBits(float tile_info_y)
{
    // R16Fテクスチャとして値を格納しているため、数値として最も近い整数へ戻す。
    // asuint はIEEE754ビット列の再解釈になり、意図した packed 値にはならない。
    return (uint)(tile_info_y + 0.5);
}

uint SspTileInfoEncodeProbePosFlatIndex(uint2 probe_pos_in_tile)
{
    return probe_pos_in_tile.x + probe_pos_in_tile.y * SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
}

uint SspTileInfoDecodeProbePosFlatIndex(float tile_info_y)
{
    return SspTileInfoYToPackedBits(tile_info_y) & k_ss_probe_tile_info_probe_pos_mask;
}

int2 SspTileInfoDecodeProbePosInTile(float tile_info_y)
{
    const uint flat_index = SspTileInfoDecodeProbePosFlatIndex(tile_info_y);
    return int2(flat_index % SCREEN_SPACE_PROBE_INFO_DOWNSCALE, flat_index / SCREEN_SPACE_PROBE_INFO_DOWNSCALE);
}

bool SspTileInfoIsReprojectionSucceeded(float tile_info_y)
{
    return 0u != (SspTileInfoYToPackedBits(tile_info_y) & k_ss_probe_tile_info_reprojection_succeeded_mask);
}

float SspTileInfoBuildY(uint probe_pos_flat_index, bool is_reprojection_succeeded)
{
    uint packed = probe_pos_flat_index & k_ss_probe_tile_info_probe_pos_mask;
    if(is_reprojection_succeeded)
    {
        packed |= k_ss_probe_tile_info_reprojection_succeeded_mask;
    }
    return (float)packed;
}

float SspTileInfoBuildY(uint2 probe_pos_in_tile, bool is_reprojection_succeeded)
{
    return SspTileInfoBuildY(SspTileInfoEncodeProbePosFlatIndex(probe_pos_in_tile), is_reprojection_succeeded);
}

float4 SspTileInfoSetReprojectionSucceeded(float4 tile_info, bool is_reprojection_succeeded)
{
    const uint probe_pos_flat_index = SspTileInfoDecodeProbePosFlatIndex(tile_info.y);
    tile_info.y = SspTileInfoBuildY(probe_pos_flat_index, is_reprojection_succeeded);
    return tile_info;
}

float4 SspTileInfoBuild(float depth, uint2 probe_pos_in_tile, float2 approx_normal_oct, bool is_reprojection_succeeded)
{
    return float4(depth, SspTileInfoBuildY(probe_pos_in_tile, is_reprojection_succeeded), approx_normal_oct.x, approx_normal_oct.y);
}

// ---- Temporal Reprojection ユーティリティ ----
uint SspPackTileId(int2 tile_id)
{
    return (uint(tile_id.y) << 16) | uint(tile_id.x & 0xffff);
}
int2 SspUnpackTileId(uint packed)
{
    return int2(int(packed & 0xffffu), int((packed >> 16) & 0xffffu));
}

// ------------------------------------------------------------------------------------------------------------------------


// ワールド法線->半球OctahedralMapエンコード.
float2 OctahedralEncodeHemisphereDirWs(float3 dir_ws, float3 base_normal_ws)
{
    return OctEncodeHemiByNormal(dir_ws, base_normal_ws);
}
// ワールド法線->球面OctahedralMapエンコード.
float2 OctahedralEncodeSphereDirWs(float3 dir_ws)
{
    return OctEncode(dir_ws);
}

// 半球OctahedralMapデコード->ワールド空間方向.
float3 OctahedralDecodeHemisphereDirWs(float2 oct_uv, float3 basis_t_ws, float3 basis_b_ws, float3 base_normal_ws)
{
    const float3 local_dir = OctDecodeHemi(oct_uv);
    return local_dir.x * basis_t_ws + local_dir.y * basis_b_ws + local_dir.z * base_normal_ws;
}
// 半球OctahedralMapデコード->ワールド空間方向. 内部で接空間構築するバージョン.
float3 OctahedralDecodeHemisphereDirWs(float2 oct_uv, float3 base_normal_ws)
{
    // 内部で接空間構築するバージョン.
    float3 basis_t_ws;
    float3 basis_b_ws;
    BuildOrthonormalBasis(base_normal_ws, basis_t_ws, basis_b_ws);
    return OctahedralDecodeHemisphereDirWs(oct_uv, basis_t_ws, basis_b_ws, base_normal_ws);
}
// 球面OctahedralMapデコード->ワールド空間方向.
float3 OctahedralDecodeSphereDirWs(float2 oct_uv)
{
    return OctDecode(oct_uv);
}



// OctahedralMapストレージ向け.
// 球面/半球切り替え用. ワールド空間方向->OctahedralMapエンコード. DirectSHモードでは半球OctahedralMap, それ以外のモードでは球面OctahedralMapを使用するため、モードに応じて切り替える.
float2 SspEncodeDirByNormal(float3 dir_ws, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralEncodeHemisphereDirWs(dir_ws, base_normal_ws);
    #else
        return OctahedralEncodeSphereDirWs(dir_ws);
    #endif
}
// OctahedralMapストレージ向け.
// 球面/半球切り替え用. UV空間方向->OctahedralMapエンコード. DirectSHモードでは半球OctahedralMap, それ以外のモードでは球面OctahedralMapを使用するため、モードに応じて切り替える. 内部で接空間構築するバージョン.
float3 SspDecodeDirByNormal(float2 oct_uv, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralDecodeHemisphereDirWs(oct_uv, base_normal_ws);
    #else
        return OctahedralDecodeSphereDirWs(oct_uv);
    #endif
}
// OctahedralMapストレージ向け.
// 球面/半球切り替え用. UV空間方向->OctahedralMapエンコード. DirectSHモードでは半球OctahedralMap, それ以外のモードでは球面OctahedralMapを使用するため、モードに応じて切り替える.
float3 SspDecodeDirByNormal(float2 oct_uv, float3 basis_t_ws, float3 basis_b_ws, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralDecodeHemisphereDirWs(oct_uv, basis_t_ws, basis_b_ws, base_normal_ws);
    #else
        return OctahedralDecodeSphereDirWs(oct_uv);
    #endif
}


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
//      voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_srvs.bbv.grid_resolution - cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_resolution)
//  という使い方で可能.
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution)
{
    return (voxel_coord + toroidal_offset) % resolution;
}

// Bbvの取り扱い.
// ------------------------------------------------------------------------------------------------------------------------
// Brick数. Toroidal offset を加味したアドレス計算の前に使う物理バッファ上の総Brick数。
uint bbv_brick_count()
{
    return cb_srvs.bbv.grid_resolution.x * cb_srvs.bbv.grid_resolution.y * cb_srvs.bbv.grid_resolution.z;
}
// HiBrickグリッド解像度. 端数は切り上げで末端HiBrickに畳み込む。
int3 bbv_hibrick_grid_resolution()
{
    return (cb_srvs.bbv.grid_resolution + int3(k_bbv_hibrick_brick_resolution - 1, k_bbv_hibrick_brick_resolution - 1, k_bbv_hibrick_brick_resolution - 1)) / k_bbv_hibrick_brick_resolution;
}
// HiBrick総数.
uint bbv_hibrick_count()
{
    const int3 hibrick_grid_resolution = bbv_hibrick_grid_resolution();
    return hibrick_grid_resolution.x * hibrick_grid_resolution.y * hibrick_grid_resolution.z;
}
// Brick座標から、それを内包する logical HiBrick 座標へ変換する。
int3 bbv_voxel_coord_to_hibrick_coord(int3 voxel_coord)
{
    return voxel_coord / k_bbv_hibrick_brick_resolution;
}
// Brick座標から HiBrick data region 用のリニアindexを得る。
// HiBrick data region は toroidal 化せず、logical 2x2x2 Brick cluster 順で保持する。
uint bbv_hibrick_index_from_voxel_coord(int3 voxel_coord)
{
    return voxel_coord_to_index(bbv_voxel_coord_to_hibrick_coord(voxel_coord), bbv_hibrick_grid_resolution());
}
// Brick index から HiBrick data region 用のリニアindexを得る。
// voxel_index は physical index 解釈になるため、logical HiBrick を参照したい箇所では
// index ではなく logical Brick 座標を直接渡す helper を優先する。
uint bbv_hibrick_index_from_voxel_index(uint voxel_index)
{
    return bbv_hibrick_index_from_voxel_coord(index_to_voxel_coord(voxel_index, cb_srvs.bbv.grid_resolution));
}
// BBV本体バッファは [bitmask region][brick data region][hibrick data region] の順。
// それぞれの base helper から絶対アドレスを導出する。
uint bbv_bitmask_region_addr_base()
{
    return 0;
}
// Brick data region は bitmask region の直後に連続配置する。
uint bbv_brick_data_region_addr_base()
{
    return bbv_brick_count() * k_bbv_per_voxel_bitmask_u32_count;
}
// HiBrick data region は brick data region の直後に連続配置する。
uint bbv_hibrick_data_region_addr_base()
{
    return bbv_brick_data_region_addr_base() + bbv_brick_count() * k_bbv_brick_data_u32_count;
}
// Brick毎のデータ部先頭アドレス計算.
uint bbv_voxel_unique_data_addr(uint voxel_index)
{
    return bbv_brick_data_region_addr_base() + voxel_index * k_bbv_brick_data_u32_count;
}
// Brick毎の occupied voxel count のアドレス計算.
// 旧 coarse occupancy flag 名を残しているのは既存呼び出し側の変更量を抑えるため。
// 現在の実体は「bitmask成分フラグ」ではなく「Brick内 occupied voxel count」。
uint bbv_voxel_coarse_occupancy_info_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 0;
}
// Brick毎の作業用データ部アドレス.
uint bbv_voxel_brick_work_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 1;
}
// HiBrick毎の occupied voxel total count のアドレス計算.
// count > 0 なら、その logical HiBrick 配下のどこかに occupied voxel が存在する。
uint bbv_hibrick_voxel_count_addr(uint hibrick_index)
{
    return bbv_hibrick_data_region_addr_base() + hibrick_index * k_bbv_hibrick_data_u32_count;
}
// logical Brick 座標から直接 HiBrick の count アドレスを引く helper.
uint bbv_hibrick_voxel_count_addr_from_voxel_coord(int3 voxel_coord)
{
    return bbv_hibrick_voxel_count_addr(bbv_hibrick_index_from_voxel_coord(voxel_coord));
}
// logical HiBrick 座標から occupied voxel total count のアドレスを引く helper.
uint bbv_hibrick_voxel_count_addr_from_hibrick_coord(int3 hibrick_coord)
{
    return bbv_hibrick_voxel_count_addr(voxel_coord_to_index(hibrick_coord, bbv_hibrick_grid_resolution()));
}
// physical Brick index から直接 HiBrick の count アドレスを引く helper.
// HiBrick data が logical cluster ベースになった後は、trace や debug のように
// logical 空間で扱う用途では使わないこと。
uint bbv_hibrick_voxel_count_addr_from_voxel_index(uint voxel_index)
{
    return bbv_hibrick_voxel_count_addr(bbv_hibrick_index_from_voxel_index(voxel_index));
}
// Brick毎の占有ビットマスクデータ先頭アドレス計算.
// bitmask region は Brick ごとに固定長で前詰め配置しているため単純な積で引ける。
uint bbv_voxel_bitmask_data_addr(uint voxel_index)
{
    return bbv_bitmask_region_addr_base() + voxel_index * k_bbv_per_voxel_bitmask_u32_count;
}
// Voxel毎の占有ビットマスクのu32単位数.
uint bbv_voxel_bitmask_uint_count()
{
    return k_bbv_per_voxel_bitmask_u32_count;
}

// Brick 内の occupied voxel count を、簡易 cone 積分で使う 0..1 の密度近似へ変換する。
// この段階では空間分布は見ず、8x8x8 内の充填率だけを使う。
float bbv_brick_occupancy_ratio_from_count(uint occupied_voxel_count)
{
    return saturate(float(occupied_voxel_count) / float(k_bbv_per_voxel_bitmask_bit_count));
}

// HiBrick(2x2x2 Brick cluster) 全体の occupied voxel total count を 0..1 の粗い密度近似へ変換する。
// まずは skip 判定や debug 可視化の補助指標として使い、直接の fine hit 判定には使わない。
float bbv_hibrick_occupancy_ratio_from_count(uint occupied_voxel_total_count)
{
    const uint hibrick_voxel_capacity = k_bbv_per_voxel_bitmask_bit_count * k_bbv_hibrick_brick_resolution * k_bbv_hibrick_brick_resolution * k_bbv_hibrick_brick_resolution;
    return saturate(float(occupied_voxel_total_count) / float(hibrick_voxel_capacity));
}

// Bbvの内部座標を元にリニアインデックスを計算.
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
// Bbvの内部座標を元にバッファの該当Voxelブロック内のオフセットと読み取りビット位置を計算.
void calc_bbv_bitcell_info(out uint out_u32_offset, out uint out_bit_location, uint3 bitcell_pos)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint bitcell_index = calc_bbv_bitcell_index(bitcell_pos);

    calc_bbv_bitcell_info_from_bitcell_index(out_u32_offset, out_bit_location, bitcell_index);
}

// Bbvのビットセルインデックスから k_bbv_per_voxel_resolution^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_bbv_per_voxel_bitmask_bit_count-1
uint3 calc_bbv_bitcell_pos_from_bit_index(uint bit_index)
{
    // 現状はX,Y,Z順のリニアレイアウト.
    const uint3 bit_pos = uint3(bit_index % k_bbv_per_voxel_resolution, (bit_index / k_bbv_per_voxel_resolution) % k_bbv_per_voxel_resolution, bit_index / (k_bbv_per_voxel_resolution * k_bbv_per_voxel_resolution));
    return bit_pos;
}


// ------------------------------------------------------------------------------------------------------------------------
// BbvのBrickデータレイアウト.

// uint[0]      : Brick内 occupied voxel count.
// uint[1].8bit : 最後に可視状態になったフレーム番号. 0-255でループ.

// ユニークデータに埋め込むためのフレーム番号マスク処理.
uint mask_bbv_voxel_unique_data_last_visible_frame(uint last_visible_frame)
{
    return (last_visible_frame & 0xff);
}

// ------------------------------------------------------------------------------------------------------------------------
// Bbv. Brickデータクリア.
void clear_voxel_data(RWBuffer<uint> bbv_buffer, uint voxel_index)
{
    const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
    // Brickデータクリア.
    for(int i = 0; i < k_bbv_brick_data_u32_count; ++i)
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
    if (t_far <= t_near || ray_len < t_near)
        return false;

    // 結果を返す. このt値で origin + dir * t を計算すればそれぞれ始点と終点がAABB空間内にクランプされた座標になる.
    out_aabb_clamped_origin_t = max(out_aabb_clamped_origin_t, t_near);
    out_aabb_clamped_end_t = min(out_aabb_clamped_end_t, t_far);

    return true;
};

// Bbv内部のビットセル単位でのレイトレース.
// https://github.com/dubiousconst282/VoxelRT
int3 trace_bitmask_brick(float3 rayPos, float3 rayDir, float3 rayDirSign, float3 invDir, inout bool3 stepMask, 
        Buffer<uint> bbv_buffer, uint bbv_bitmask_addr,
        
        const bool intersection_bit_mode, // Bbvの占有状態のどちらと交差をするか指定する. true:通常通り占有されたVoxelと交差, false:非占有Voxelと交差.

        const bool static_enable_initial_hit_avoidance,
        int initial_hit_avoidance_count,

        const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
    ) 
{
    rayPos = clamp(rayPos, 0.0001, float(k_bbv_per_voxel_resolution)-0.0001);

    float3 sideDist = ((floor(rayPos) - rayPos) + step(0.0, rayDir)) * invDir;
    int3 mapPos = int3(floor(rayPos));

    int3 raySign = rayDirSign;
    if(!is_brick_mode)
    {
        do {
            uint bitcell_u32_offset, bitcell_u32_bit_pos;
            calc_bbv_bitcell_info(bitcell_u32_offset, bitcell_u32_bit_pos, mapPos);
            bool is_hit = (0!= (bbv_buffer[bbv_bitmask_addr + bitcell_u32_offset] & (1u << bitcell_u32_bit_pos)));

            if(static_enable_initial_hit_avoidance)
            {
                // 初期ヒット回避処理.
                if(intersection_bit_mode == is_hit) 
                {
                    // ヒットした場合でも初期ヒット回避カウントが残っていれば無視.
                    if(0 >= initial_hit_avoidance_count)
                        return mapPos;

                    initial_hit_avoidance_count--;// カウントダウン.
                }
                else
                {
                    // ヒットしなくなった時点で通常ヒットモードに即座に移行.
                    initial_hit_avoidance_count = 0;
                }
            }
            else
            {
                // 通常ヒット処理.
                if(intersection_bit_mode == is_hit)
                    return mapPos;
            }

            stepMask = calc_dda_trace_step_mask(sideDist);
            sideDist += select(stepMask, abs(invDir), 0);
            const int3 mapPosDelta = select(stepMask, raySign, 0);
            mapPos += mapPosDelta;
            //if(all(mapPosDelta == 0)) {break;}// 外側のBrick単位ループではこのチェックが必要だがここでは不要そう.
            
        } while (all(uint3(mapPos) < k_bbv_per_voxel_resolution));

        return -1;
    }
    else
    {
        return mapPos;// デバッグ用にBrick単位で即時ヒット扱いする. この関数に入る時点でBrick単位のOccupiedフラグを参照しているはず.
    }
}

float4 trace_bbv_core(
    out int out_hit_voxel_index,
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws,
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const bool intersection_bit_mode,
    const int static_initial_hit_avoidance_count,
    const bool is_brick_mode
);

// BBV trace 用の ray_dir reciprocal。
// もともとは ray_dir の 0 軸を巨大値へ置き換える safe reciprocal を試したが、
// HiBrick 有無に関係なく Bbv デバッグ表示で特定カメラ角度に 1px の横線欠けが発生した。
// DDA 側を切り分けた結果 reciprocal の扱いが原因と分かり、旧来実装の 1.0 / ray_dir に戻している。
// 現状は運用実績のある旧挙動を優先し、境界判定の安定性を保つ。
float3 calc_safe_trace_ray_dir_inv(float3 ray_dir)
{
    return 1.0 / ray_dir;
}

// レイ上の現在 t 位置から、次のセル内側を確実にサンプルするための t を計算.
float calc_trace_sample_t(float curr_t, float end_t)
{
    const float k_trace_t_epsilon = 1e-4;
    return min(curr_t + k_trace_t_epsilon, max(curr_t, end_t - k_trace_t_epsilon));
}

// 等間隔グリッドのセル境界を取得.
void calc_trace_grid_cell_bounds(out float3 out_cell_min, out float3 out_cell_max, int3 cell_coord, int cell_span, int3 full_grid_resolution)
{
    const int3 cell_coord_min = cell_coord * cell_span;
    const int3 cell_coord_max = min(cell_coord_min + int3(cell_span, cell_span, cell_span), full_grid_resolution);
    out_cell_min = float3(cell_coord_min);
    out_cell_max = float3(cell_coord_max);
}

// レイ上 current_t 以降でセルと交差する区間を計算.
bool calc_trace_cell_t_range(
    out float out_cell_begin_t,
    out float out_cell_end_t,
    float3 ray_origin,
    float3 ray_dir,
    float3 ray_dir_inv,
    float ray_end_t,
    float3 cell_min,
    float3 cell_max,
    float current_t
)
{
    out_cell_begin_t = 0.0;
    out_cell_end_t = 0.0;

    float cell_begin_t;
    float cell_end_t;
    if(!calc_ray_t_offset_for_aabb(cell_begin_t, cell_end_t, cell_min, cell_max, ray_origin, ray_dir, ray_dir_inv, ray_end_t))
    {
        return false;
    }

    out_cell_begin_t = max(cell_begin_t, current_t);
    out_cell_end_t = min(cell_end_t, ray_end_t);
    return out_cell_begin_t <= out_cell_end_t;
}

// 現在セルから見た各軸の次境界までの t を計算.
float3 calc_trace_grid_next_boundary_t(
    float3 ray_origin,
    float3 ray_dir_inv,
    float3 ray_step_offset,
    int3 cell_coord,
    int cell_span,
    int3 full_grid_resolution)
{
    const int3 cell_coord_min = cell_coord * cell_span;
    const int3 cell_coord_max = min(cell_coord_min + int3(cell_span, cell_span, cell_span), full_grid_resolution);
    const float3 next_boundary = select(ray_step_offset > 0.0, float3(cell_coord_max), float3(cell_coord_min));
    return (next_boundary - ray_origin) * ray_dir_inv;
}

// レイ上の t 位置をサンプルして、その時点で属しているグリッドセル座標を返す.
int3 calc_trace_grid_coord_from_t(float3 ray_origin, float3 ray_dir, float sample_t, int cell_span, int3 grid_resolution)
{
    const float3 sample_pos = ray_origin + ray_dir * sample_t;
    const int3 coord = int3(floor(sample_pos / float(cell_span)));
    return clamp(coord, int3(0, 0, 0), grid_resolution - 1);
}

// DDA で訪れるセル数の理論上限を、始点セルと終点セルの差から見積もる。
int calc_trace_grid_max_iteration_count(int3 begin_coord, int3 end_coord)
{
    const int3 trace_extent = abs(end_coord - begin_coord) + 1;
    return max(1, trace_extent.x + trace_extent.y + trace_extent.z) + 2;
}

// BBV の Brick 単位 DDA を走らせる共通処理.
bool trace_bbv_brick_dda_range(
    out int3 out_hit_map_pos,
    out int3 out_hit_sub_map_pos,
    out bool3 out_hit_step_mask,
    inout int inout_initial_hit_avoidance_count,
    inout uint inout_brick_check_count,
    inout uint inout_bitmask_check_count,
    float3 clampled_start_pos,
    float3 ray_dir_ws,
    float3 ray_dir_sign,
    float3 ray_dir_inv,
    int3 ray_step,
    float3 ray_step_offset,
    float trace_begin_t,
    float trace_end_t,
    int3 brick_coord_min,
    int3 brick_coord_max,
    int3 grid_resolution,
    int3 bbv_grid_toroidal_offset,
    Buffer<uint> bbv_buffer,
    const bool intersection_bit_mode,
    const bool enable_initial_hit_avoidance,
    const bool is_brick_mode
)
{
    const float k_trace_t_epsilon = 1e-4;
    out_hit_map_pos = int3(-1, -1, -1);
    out_hit_sub_map_pos = int3(-1, -1, -1);
    out_hit_step_mask = bool3(false, false, false);

    const int3 brick_extent = brick_coord_max - brick_coord_min;
    const int max_brick_iteration_count = max(1, brick_extent.x + brick_extent.y + brick_extent.z) + 2;

    float brick_curr_t = trace_begin_t;
    const float brick_sample_t = calc_trace_sample_t(brick_curr_t, trace_end_t);
    int3 map_pos = clamp(calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, brick_sample_t, 1, grid_resolution), brick_coord_min, brick_coord_max - 1);
    float3 brick_next_t = calc_trace_grid_next_boundary_t(
        clampled_start_pos,
        ray_dir_inv,
        ray_step_offset,
        map_pos,
        1,
        grid_resolution);
    [loop]
    for(int brick_iter = 0; brick_iter < max_brick_iteration_count && brick_curr_t <= trace_end_t; ++brick_iter)
    {
        const float brick_begin_t = brick_curr_t;
        const float brick_end_t = min(Min3(brick_next_t), trace_end_t);

        const int3 toroidal_map_pos = voxel_coord_toroidal_mapping(map_pos, bbv_grid_toroidal_offset, grid_resolution);
        const uint voxel_index = voxel_coord_to_index(toroidal_map_pos, grid_resolution);
        inout_brick_check_count++;
        const bool bbv_occupied_flag = (0 != bbv_buffer[bbv_voxel_coarse_occupancy_info_addr(voxel_index)]);
        if(!intersection_bit_mode || bbv_occupied_flag)
        {
            const float3 brick_entry_pos = clampled_start_pos + ray_dir_ws * brick_begin_t;
            const float3 pos_in_brick = (brick_entry_pos - map_pos) * k_bbv_per_voxel_resolution;

            inout_bitmask_check_count++;
            bool3 detail_step_mask = bool3(false, false, false);
            const int3 sub_map_pos = trace_bitmask_brick(
                pos_in_brick,
                ray_dir_ws,
                ray_dir_sign,
                ray_dir_inv,
                detail_step_mask,
                bbv_buffer,
                bbv_voxel_bitmask_data_addr(voxel_index),
                intersection_bit_mode,
                enable_initial_hit_avoidance,
                inout_initial_hit_avoidance_count,
                is_brick_mode);
            if(sub_map_pos.x >= 0)
            {
                out_hit_map_pos = map_pos;
                out_hit_sub_map_pos = sub_map_pos;
                out_hit_step_mask = detail_step_mask;
                return true;
            }
        }

        if(enable_initial_hit_avoidance)
        {
            inout_initial_hit_avoidance_count--;
        }

        const bool3 brick_step_mask = calc_dda_trace_step_mask(brick_next_t);
        const int3 map_pos_delta = select(brick_step_mask, ray_step, 0);
        map_pos += map_pos_delta;
        brick_curr_t = max(brick_curr_t + k_trace_t_epsilon, brick_end_t + k_trace_t_epsilon);
        if((any(map_pos < brick_coord_min) || any(map_pos >= brick_coord_max)) || all(map_pos_delta == 0))
        {
            break;
        }
        brick_next_t = calc_trace_grid_next_boundary_t(
            clampled_start_pos,
            ray_dir_inv,
            ray_step_offset,
            map_pos,
            1,
            grid_resolution);
    }

    return false;
}

// Brick 範囲だけを DDA で走査し、occupied Brick の充填率を使って透過率を積分する。
// fine voxel の詳細 hit は取らず、Brick occupancy ratio を区間長へ掛けて optical depth を近似する。
// transmittance_stop_threshold 以下まで透過率が落ちたら、十分不透明とみなして早期終了する。
// 戻り値 true は「十分不透明になったので外側ループを打ち切ってよい」を意味する。
bool trace_bbv_brick_transmittance_range(
    inout float inout_transmittance,
    inout float inout_accumulated_optical_depth,
    inout uint inout_brick_trace_count,
    inout float inout_brick_occupancy_ratio_sum,
    float3 clampled_start_pos,
    float3 ray_dir_ws,
    float3 ray_dir_inv,
    int3 ray_step,
    float3 ray_step_offset,
    float trace_begin_t,
    float trace_end_t,
    int3 brick_coord_min,
    int3 brick_coord_max,
    int3 grid_resolution,
    int3 bbv_grid_toroidal_offset,
    Buffer<uint> bbv_buffer,
    const float transmittance_stop_threshold
)
{
    const float k_trace_t_epsilon = 1e-4;

    const int3 brick_extent = brick_coord_max - brick_coord_min;
    const int max_brick_iteration_count = max(1, brick_extent.x + brick_extent.y + brick_extent.z) + 2;

    float brick_curr_t = trace_begin_t;
    int3 map_pos = clamp(calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, calc_trace_sample_t(brick_curr_t, trace_end_t), 1, grid_resolution), brick_coord_min, brick_coord_max - 1);
    float3 brick_next_t = calc_trace_grid_next_boundary_t(
        clampled_start_pos,
        ray_dir_inv,
        ray_step_offset,
        map_pos,
        1,
        grid_resolution);
    [loop]
    for(int brick_iter = 0; brick_iter < max_brick_iteration_count && brick_curr_t <= trace_end_t; ++brick_iter)
    {
        const float brick_begin_t = brick_curr_t;
        const float brick_end_t = min(Min3(brick_next_t), trace_end_t);
        const float brick_segment_t = max(0.0, brick_end_t - brick_begin_t);

        const int3 toroidal_map_pos = voxel_coord_toroidal_mapping(map_pos, bbv_grid_toroidal_offset, grid_resolution);
        const uint voxel_index = voxel_coord_to_index(toroidal_map_pos, grid_resolution);
        const uint brick_occupied_voxel_count = bbv_buffer[bbv_voxel_coarse_occupancy_info_addr(voxel_index)];
        if(0 != brick_occupied_voxel_count)
        {
            const float brick_occupancy_ratio = bbv_brick_occupancy_ratio_from_count(brick_occupied_voxel_count);
            // Brick 区間全体が一様密度だったとみなして optical depth を積む。
            const float brick_optical_depth = brick_occupancy_ratio * brick_segment_t;
            inout_accumulated_optical_depth += brick_optical_depth;
            inout_transmittance *= exp(-brick_optical_depth);
            inout_brick_trace_count++;
            inout_brick_occupancy_ratio_sum += brick_occupancy_ratio;
            if(inout_transmittance <= transmittance_stop_threshold)
            {
                inout_transmittance = 0.0;
                return true;
            }
        }

        const bool3 brick_step_mask = calc_dda_trace_step_mask(brick_next_t);
        const int3 map_pos_delta = select(brick_step_mask, ray_step, 0);
        map_pos += map_pos_delta;
        brick_curr_t = max(brick_curr_t + k_trace_t_epsilon, brick_end_t + k_trace_t_epsilon);
        if((any(map_pos < brick_coord_min) || any(map_pos >= brick_coord_max)) || all(map_pos_delta == 0))
        {
            break;
        }
        brick_next_t = calc_trace_grid_next_boundary_t(
            clampled_start_pos,
            ray_dir_inv,
            ray_step_offset,
            map_pos,
            1,
            grid_resolution);
    }

    return false;
}

float4 trace_bbv_build_hit_result(
    out int out_hit_voxel_index,
    int3 hit_map_pos,
    int3 hit_sub_map_pos,
    bool3 hit_step_mask,
    float3 clampled_start_pos,
    float3 ray_dir_sign,
    float3 ray_dir_inv,
    float3 ray_component_validity,
    float ray_trace_begin_t_offset,
    float cell_width_ws,
    int3 bbv_grid_toroidal_offset,
    int3 grid_resolution
)
{
    const float3 final_pos = hit_map_pos * k_bbv_per_voxel_resolution + hit_sub_map_pos;
    const float3 start_pos = clampled_start_pos * k_bbv_per_voxel_resolution;
    const float3 mini = ((final_pos - start_pos) + 0.5 * ray_component_validity - 0.5 * ray_dir_sign) * ray_dir_inv;
    const float hit_t = max(0.0, Max3(mini) * k_bbv_per_voxel_resolution_inv);

    out_hit_voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(hit_map_pos, bbv_grid_toroidal_offset, grid_resolution), grid_resolution);
    const float3 hit_normal = select(hit_step_mask, -ray_dir_sign, 0.0);
    const float hit_t_ws = (hit_t + ray_trace_begin_t_offset) * cell_width_ws;
    return float4(hit_t_ws, hit_normal.x, hit_normal.y, hit_normal.z);
}

// Bbvレイトレース. HiBrick を最上位 accelerator とする別実装版.
// out_debug は x: empty HiBrick skip count, y: occupied HiBrick descend count,
// z: Brick coarse check count, w: bitmask/detail check count を返す。
float4 trace_bbv_hibrick_core(
    out int out_hit_voxel_index,
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws,
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const bool intersection_bit_mode,
    const int static_initial_hit_avoidance_count,
    const bool static_enable_hibrick_skip,
    const bool is_brick_mode
)
{
    // inverse bit trace は HiBrick count の意味が変わるため既存実装へフォールバック.
    if(!intersection_bit_mode)
    {
        return trace_bbv_core(
            out_hit_voxel_index,
            out_debug,
            ray_origin_ws, ray_dir_ws, trace_distance_ws,
            grid_min_ws, cell_width_ws, grid_resolution,
            bbv_grid_toroidal_offset, bbv_buffer,
            intersection_bit_mode,
            static_initial_hit_avoidance_count,
            is_brick_mode);
    }

    const float cell_width_ws_inv = 1.0 / cell_width_ws;

    out_hit_voxel_index = -1;
    out_debug = float4(0.0, 0.0, 0.0, 0.0);

    const float3 ray_dir_inv = calc_safe_trace_ray_dir_inv(ray_dir_ws);
    const float3 ray_dir_sign = sign(ray_dir_ws);
    const int3 ray_step = int3(ray_dir_sign);
    const float3 ray_step_offset = step(0.0, ray_dir_ws);
    const float3 ray_component_validity = abs(ray_dir_sign);

    const float3 ray_origin = (ray_origin_ws - grid_min_ws) * cell_width_ws_inv;
    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, float3(0.0, 0.0, 0.0), float3(grid_resolution), ray_origin, ray_dir_ws, ray_dir_inv, trace_distance_ws * cell_width_ws_inv))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);
    }

    const float3 clampled_start_pos = ray_origin + ray_dir_ws * ray_trace_begin_t_offset;
    const float trace_t_end = ray_trace_end_t_offset - ray_trace_begin_t_offset;
    if(trace_t_end <= 0.0)
    {
        return float4(-1.0, -1.0, -1.0, -1.0);
    }

    const bool enable_initial_hit_avoidance = (0 < static_initial_hit_avoidance_count);
    int initial_hit_avoidance_count = static_initial_hit_avoidance_count;

    const int3 hibrick_grid_resolution = bbv_hibrick_grid_resolution();
    const float k_trace_t_epsilon = 1e-4;

    int3 hit_map_pos = int3(-1, -1, -1);
    int3 hit_sub_map_pos = int3(-1, -1, -1);
    bool3 hit_step_mask = bool3(false, false, false);
    bool is_hit = false;
    uint empty_hibrick_skip_count = 0;
    uint occupied_hibrick_descend_count = 0;
    uint brick_check_count = 0;
    uint bitmask_check_count = 0;

    float curr_t = 0.0;
    int3 hibrick_coord = calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, calc_trace_sample_t(0.0, trace_t_end), k_bbv_hibrick_brick_resolution, hibrick_grid_resolution);
    const int3 hibrick_end_coord = calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, trace_t_end, k_bbv_hibrick_brick_resolution, hibrick_grid_resolution);
    const int max_hibrick_iteration_count = calc_trace_grid_max_iteration_count(hibrick_coord, hibrick_end_coord);
    float3 hibrick_next_t = calc_trace_grid_next_boundary_t(
        clampled_start_pos,
        ray_dir_inv,
        ray_step_offset,
        hibrick_coord,
        k_bbv_hibrick_brick_resolution,
        grid_resolution);
    // HiBrick の DDA で大まかに走査し、occupied HiBrick に入った時だけ
    // 共通の Brick DDA へ降りて詳細判定する。
    [loop]
    for(int hibrick_iter = 0; hibrick_iter < max_hibrick_iteration_count && curr_t <= trace_t_end; ++hibrick_iter)
    {
        const float hibrick_begin_t = curr_t;
        const float hibrick_end_t = min(Min3(hibrick_next_t), trace_t_end);

        const int3 brick_coord_min = hibrick_coord * k_bbv_hibrick_brick_resolution;
        const int3 brick_coord_max = min(brick_coord_min + int3(k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution), grid_resolution);
        const uint hibrick_index = voxel_coord_to_index(hibrick_coord, hibrick_grid_resolution);
        const uint hibrick_occupied_voxel_count = bbv_buffer[bbv_hibrick_voxel_count_addr(hibrick_index)];
        if(!static_enable_hibrick_skip || (0 != hibrick_occupied_voxel_count))
        {
            occupied_hibrick_descend_count++;

            if(trace_bbv_brick_dda_range(
                hit_map_pos,
                hit_sub_map_pos,
                hit_step_mask,
                initial_hit_avoidance_count,
                brick_check_count,
                bitmask_check_count,
                clampled_start_pos,
                ray_dir_ws,
                ray_dir_sign,
                ray_dir_inv,
                ray_step,
                ray_step_offset,
                hibrick_begin_t,
                hibrick_end_t,
                brick_coord_min,
                brick_coord_max,
                grid_resolution,
                bbv_grid_toroidal_offset,
                bbv_buffer,
                intersection_bit_mode,
                enable_initial_hit_avoidance,
                is_brick_mode))
            {
                is_hit = true;
                break;
            }
        }
        else if(static_enable_hibrick_skip)
        {
            empty_hibrick_skip_count++;
        }

        const bool3 hibrick_step_mask = calc_dda_trace_step_mask(hibrick_next_t);
        const int3 hibrick_coord_delta = select(hibrick_step_mask, ray_step, 0);
        hibrick_coord += hibrick_coord_delta;
        curr_t = max(curr_t + k_trace_t_epsilon, hibrick_end_t + k_trace_t_epsilon);
        if((any(hibrick_coord < 0) || any(hibrick_coord >= hibrick_grid_resolution)) || all(hibrick_coord_delta == 0))
        {
            break;
        }
        hibrick_next_t = calc_trace_grid_next_boundary_t(
            clampled_start_pos,
            ray_dir_inv,
            ray_step_offset,
            hibrick_coord,
            k_bbv_hibrick_brick_resolution,
            grid_resolution);
    }

    // x: empty HiBrick skip count, y: occupied HiBrick descend count,
    // z: Brick coarse check count, w: bitmask/detail check count.
    out_debug = float4(
        float(empty_hibrick_skip_count),
        float(occupied_hibrick_descend_count),
        float(brick_check_count),
        float(bitmask_check_count));

    if(hit_sub_map_pos.x >= 0)
    {
        return trace_bbv_build_hit_result(
            out_hit_voxel_index,
            hit_map_pos,
            hit_sub_map_pos,
            hit_step_mask,
            clampled_start_pos,
            ray_dir_sign,
            ray_dir_inv,
            ray_component_validity,
            ray_trace_begin_t_offset,
            cell_width_ws,
            bbv_grid_toroidal_offset,
            grid_resolution);
    }

    return float4(-1.0, -1.0, -1.0, -1.0);
}

// HiBrick を使って occupied Brick の充填率を積分し、透過率を返す簡易 voxel cone trace。
// まずは HiBrick occupancy を空間スキップに使い、occupied HiBrick 内では Brick occupancy だけで透過率を積分する。
// transmittance_stop_threshold は「ここまで暗くなったら十分」とみなして打ち切るための閾値。
//  0.9などを指定することで大まかな遮蔽計算をFineVoxelトレースより高速に実行可能.
// return.x = transmittance, y = 平均 HiBrick occupancy ratio, z = 平均 Brick occupancy ratio, w = opacity。
// out_debug.x = empty HiBrick skip count, y = occupied HiBrick descend count, z = traced Brick count, w = accumulated optical depth。
float4 trace_bbv_hibrick_brick_transmittance_core(
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws,
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const bool static_enable_hibrick_skip,
    const float transmittance_stop_threshold
)
{
    const float cell_width_ws_inv = 1.0 / cell_width_ws;

    out_debug = float4(0.0, 0.0, 0.0, 0.0);

    const float3 ray_dir_inv = calc_safe_trace_ray_dir_inv(ray_dir_ws);
    const int3 ray_step = int3(sign(ray_dir_ws));
    const float3 ray_step_offset = step(0.0, ray_dir_ws);

    const float3 ray_origin = (ray_origin_ws - grid_min_ws) * cell_width_ws_inv;
    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, float3(0.0, 0.0, 0.0), float3(grid_resolution), ray_origin, ray_dir_ws, ray_dir_inv, trace_distance_ws * cell_width_ws_inv))
    {
        return float4(1.0, 0.0, 0.0, 0.0);
    }

    const float3 clampled_start_pos = ray_origin + ray_dir_ws * ray_trace_begin_t_offset;
    const float trace_t_end = ray_trace_end_t_offset - ray_trace_begin_t_offset;
    if(trace_t_end <= 0.0)
    {
        return float4(1.0, 0.0, 0.0, 0.0);
    }

    const int3 hibrick_grid_resolution = bbv_hibrick_grid_resolution();
    const float k_trace_t_epsilon = 1e-4;

    uint empty_hibrick_skip_count = 0;
    uint occupied_hibrick_descend_count = 0;
    uint traced_brick_count = 0;
    uint sampled_hibrick_count = 0;
    float hibrick_occupancy_ratio_sum = 0.0;
    float brick_occupancy_ratio_sum = 0.0;
    float accumulated_optical_depth = 0.0;
    float transmittance = 1.0;

    float curr_t = 0.0;
    int3 hibrick_coord = calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, calc_trace_sample_t(0.0, trace_t_end), k_bbv_hibrick_brick_resolution, hibrick_grid_resolution);
    const int3 hibrick_end_coord = calc_trace_grid_coord_from_t(clampled_start_pos, ray_dir_ws, trace_t_end, k_bbv_hibrick_brick_resolution, hibrick_grid_resolution);
    const int max_hibrick_iteration_count = calc_trace_grid_max_iteration_count(hibrick_coord, hibrick_end_coord);
    float3 hibrick_next_t = calc_trace_grid_next_boundary_t(
        clampled_start_pos,
        ray_dir_inv,
        ray_step_offset,
        hibrick_coord,
        k_bbv_hibrick_brick_resolution,
        grid_resolution);
    [loop]
    for(int hibrick_iter = 0; hibrick_iter < max_hibrick_iteration_count && curr_t <= trace_t_end; ++hibrick_iter)
    {
        const float hibrick_begin_t = curr_t;
        const float hibrick_end_t = min(Min3(hibrick_next_t), trace_t_end);

        const int3 brick_coord_min = hibrick_coord * k_bbv_hibrick_brick_resolution;
        const int3 brick_coord_max = min(brick_coord_min + int3(k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution, k_bbv_hibrick_brick_resolution), grid_resolution);
        const uint hibrick_index = voxel_coord_to_index(hibrick_coord, hibrick_grid_resolution);
        const uint hibrick_occupied_voxel_count = bbv_buffer[bbv_hibrick_voxel_count_addr(hibrick_index)];
        const float hibrick_occupancy_ratio = bbv_hibrick_occupancy_ratio_from_count(hibrick_occupied_voxel_count);
        if(!static_enable_hibrick_skip || (0 != hibrick_occupied_voxel_count))
        {
            // occupied HiBrick に入った時だけ Brick レベルへ降り、leaf 側の密度近似を使う。
            occupied_hibrick_descend_count++;
            sampled_hibrick_count++;
            hibrick_occupancy_ratio_sum += hibrick_occupancy_ratio;

            if(trace_bbv_brick_transmittance_range(
                transmittance,
                accumulated_optical_depth,
                traced_brick_count,
                brick_occupancy_ratio_sum,
                clampled_start_pos,
                ray_dir_ws,
                ray_dir_inv,
                ray_step,
                ray_step_offset,
                hibrick_begin_t,
                hibrick_end_t,
                brick_coord_min,
                brick_coord_max,
                grid_resolution,
                bbv_grid_toroidal_offset,
                bbv_buffer,
                transmittance_stop_threshold))
            {
                break;
            }
        }
        else if(static_enable_hibrick_skip)
        {
            empty_hibrick_skip_count++;
        }

        const bool3 hibrick_step_mask = calc_dda_trace_step_mask(hibrick_next_t);
        const int3 hibrick_coord_delta = select(hibrick_step_mask, ray_step, 0);
        hibrick_coord += hibrick_coord_delta;
        curr_t = max(curr_t + k_trace_t_epsilon, hibrick_end_t + k_trace_t_epsilon);
        if((any(hibrick_coord < 0) || any(hibrick_coord >= hibrick_grid_resolution)) || all(hibrick_coord_delta == 0))
        {
            break;
        }
        hibrick_next_t = calc_trace_grid_next_boundary_t(
            clampled_start_pos,
            ray_dir_inv,
            ray_step_offset,
            hibrick_coord,
            k_bbv_hibrick_brick_resolution,
            grid_resolution);
    }

    out_debug = float4(
        float(empty_hibrick_skip_count),
        float(occupied_hibrick_descend_count),
        float(traced_brick_count),
        accumulated_optical_depth);

    // 返り値は後段のデバッグ可視化で解釈しやすいよう、透過率と平均 occupancy を並べて返す。
    const float average_hibrick_occupancy_ratio = (0 < sampled_hibrick_count) ? (hibrick_occupancy_ratio_sum / float(sampled_hibrick_count)) : 0.0;
    const float average_brick_occupancy_ratio = (0 < traced_brick_count) ? (brick_occupancy_ratio_sum / float(traced_brick_count)) : 0.0;
    return float4(transmittance, average_hibrick_occupancy_ratio, average_brick_occupancy_ratio, 1.0 - transmittance);
}


// Bbvレイトレース.
float4 trace_bbv_core(
    out int out_hit_voxel_index,
    out float4 out_debug,

    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws, 
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,

    const bool intersection_bit_mode, // Bbvの占有状態のどちらと交差をするか指定する. true:通常通り占有されたVoxelと交差, false:非占有Voxelと交差.

    const int static_initial_hit_avoidance_count, // 始点からヒットしている場合に無視するヒット回数. 自己遮蔽回避などに利用. 0で無効.

    const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
)
{
    const float cell_width_ws_inv = 1.0 / cell_width_ws;

    out_hit_voxel_index = -1;
    out_debug = float4(0.0, 0.0, 0.0, 0.0);

    const float3 ray_dir_inv = calc_safe_trace_ray_dir_inv(ray_dir_ws);
    const float3 ray_dir_sign = sign(ray_dir_ws);
    const int3 ray_step = int3(ray_dir_sign);
    const float3 ray_step_offset = step(0.0, ray_dir_ws);
    const float3 ray_component_validity = abs(ray_dir_sign);

    const float3 ray_origin = (ray_origin_ws - grid_min_ws) * cell_width_ws_inv;
    float ray_trace_begin_t_offset;
    float ray_trace_end_t_offset;
    if(!calc_ray_t_offset_for_aabb(ray_trace_begin_t_offset, ray_trace_end_t_offset, float3(0.0, 0.0, 0.0), float3(grid_resolution), ray_origin, ray_dir_ws, ray_dir_inv, trace_distance_ws * cell_width_ws_inv))
    {
        return float4(-1.0, -1.0, -1.0, -1.0);
    }

    const float3 clampled_start_pos = ray_origin + ray_dir_ws * ray_trace_begin_t_offset;
    const float3 clampled_end_pos = ray_origin + ray_dir_ws * ray_trace_end_t_offset;
    const float trace_t_end = ray_trace_end_t_offset - ray_trace_begin_t_offset;
    if(trace_t_end <= 0.0)
    {
        return float4(-1.0, -1.0, -1.0, -1.0);
    }

    const int3 trace_cell_min = min(int3(floor(clampled_start_pos)), int3(floor(clampled_end_pos)));
    const int3 trace_cell_max = max(int3(floor(clampled_start_pos)), int3(floor(clampled_end_pos))) + 1;

    const bool enable_initial_hit_avoidance = (0 < static_initial_hit_avoidance_count);
    int initial_hit_avoidance_count = static_initial_hit_avoidance_count;
    int3 hit_map_pos = int3(-1, -1, -1);
    int3 hit_sub_map_pos = int3(-1, -1, -1);
    bool3 hit_step_mask = bool3(false, false, false);
    uint brick_check_count = 0;
    uint bitmask_check_count = 0;

    if(trace_bbv_brick_dda_range(
        hit_map_pos,
        hit_sub_map_pos,
        hit_step_mask,
        initial_hit_avoidance_count,
        brick_check_count,
        bitmask_check_count,
        clampled_start_pos,
        ray_dir_ws,
        ray_dir_sign,
        ray_dir_inv,
        ray_step,
        ray_step_offset,
        0.0,
        trace_t_end,
        trace_cell_min,
        trace_cell_max,
        grid_resolution,
        bbv_grid_toroidal_offset,
        bbv_buffer,
        intersection_bit_mode,
        enable_initial_hit_avoidance,
        is_brick_mode))
    {
        out_debug = float4(0.0, 0.0, float(brick_check_count), float(bitmask_check_count));
        return trace_bbv_build_hit_result(
            out_hit_voxel_index,
            hit_map_pos,
            hit_sub_map_pos,
            hit_step_mask,
            clampled_start_pos,
            ray_dir_sign,
            ray_dir_inv,
            ray_component_validity,
            ray_trace_begin_t_offset,
            cell_width_ws,
            bbv_grid_toroidal_offset,
            grid_resolution);
    }

    out_debug = float4(0.0, 0.0, float(brick_check_count), float(bitmask_check_count));

    return float4(-1.0, -1.0, -1.0, -1.0);
}




// 標準の BBV トレース入口. 従来どおり Brick / bitmask を全域走査する。
// HiBrickスキップのオーバーヘッドがない分だけ短距離トレースは高速.
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
        true, // 通常モード.
        0, // 初期ヒット回避無効.
        false
    );
}
// 開発用 BBV トレース入口. is_brick_mode で Brick coarse hit のみを見る。
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
        true, // 通常モード.
        0, // 初期ヒット回避無効.
        is_brick_mode
    );
}
// 開発用 BBV トレース入口. HiBrick skip の統計も取りたいデバッグ表示向け。
float4 trace_bbv_dev_hibrick(
    out int out_hit_voxel_index,
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws,
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const bool is_brick_mode
)
{
    return trace_bbv_hibrick_core(
        out_hit_voxel_index,
        out_debug,
        ray_origin_ws, ray_dir_ws, trace_distance_ws,
        grid_min_ws, cell_width_ws, grid_resolution,
        bbv_grid_toroidal_offset, bbv_buffer,
        true,
        0,
        true,
        is_brick_mode
    );
}
// 開発用 BBV voxel cone trace 入口。
// 本番の cone 実装を入れる前に、HiBrick skip と Brick occupancy 積分の見え方をデバッグ表示で確認するための入口。
float4 trace_bbv_dev_hibrick_brick_transmittance(
    out float4 out_debug,
    float3 ray_origin_ws, float3 ray_dir_ws, float trace_distance_ws,
    float3 grid_min_ws, float cell_width_ws, int3 grid_resolution,
    int3 bbv_grid_toroidal_offset, Buffer<uint> bbv_buffer,
    const float transmittance_stop_threshold
)
{
    return trace_bbv_hibrick_brick_transmittance_core(
        out_debug,
        ray_origin_ws, ray_dir_ws, trace_distance_ws,
        grid_min_ws, cell_width_ws, grid_resolution,
        bbv_grid_toroidal_offset, bbv_buffer,
        true,
        transmittance_stop_threshold
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


#endif // NGL_SHADER_SRVS_UTIL_H

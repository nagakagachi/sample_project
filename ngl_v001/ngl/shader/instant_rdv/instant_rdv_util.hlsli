#ifndef NGL_SHADER_INSTANT_RDV_UTIL_H
#define NGL_SHADER_INSTANT_RDV_UTIL_H

#if 0

instant_rdv_util.hlsli

#endif


#include "../include/math_util.hlsli"
#include "../include/rand_util.hlsli"
#include "../include/bit_util.hlsli"

// cpp/hlsl共通定義用ヘッダ.
#include "instant_rdv_common_header.hlsli"

uint2 ReducedSurfaceBufferJitterOffset(uint2 tile_coord, uint frame_index)
{
    // Tileごとに位相をずらしつつ、16フレームで4x4の全texelを必ず一巡する。
    const uint tile_phase =
        (tile_coord.x * 3u + tile_coord.y * 5u) &
        (k_reduced_surface_buffer_downscale *
         k_reduced_surface_buffer_downscale - 1u);
    const uint sample_index =
        (frame_index + tile_phase) &
        (k_reduced_surface_buffer_downscale *
         k_reduced_surface_buffer_downscale - 1u);
    return uint2(
        sample_index & (k_reduced_surface_buffer_downscale - 1u),
        sample_index >> 2u);
}

uint2 ReducedSurfaceBufferSourceTexel(
    uint2 sample_coord,
    uint2 source_resolution,
    uint frame_index)
{
    return min(
        sample_coord * k_reduced_surface_buffer_downscale +
            ReducedSurfaceBufferJitterOffset(sample_coord, frame_index),
        source_resolution - 1u);
}

// WaveActiveBallot結果を扱う共通ヘルパー.
// uint4(x,y,z,w) を使い、32/64/128 laneを同じ実装で扱う。
uint first_lane_from_ballot(uint4 ballot)
{
    if(ballot.x != 0u) return firstbitlow(ballot.x);
    if(ballot.y != 0u) return 32u + firstbitlow(ballot.y);
    if(ballot.z != 0u) return 64u + firstbitlow(ballot.z);
    return 96u + firstbitlow(ballot.w);
}

bool ballot_any(uint4 ballot)
{
    return (ballot.x | ballot.y | ballot.z | ballot.w) != 0u;
}


// Probe更新系のCS ThreadGroupSize. Indirectのため共有ヘッダに定義.
// SharedMemのサイズ制限のため調整.
#define PROBE_UPDATE_THREAD_GROUP_SIZE 96


// ------------------------------------------------------------------------------------------------------------------------
// Bbv. bbv.
// 1. Voxel bit data region
// 2. Brick data region
Buffer<uint>		BitmaskBrickVoxel;
RWBuffer<uint>		RWBitmaskBrickVoxel;

// Bbv毎の追加データ.
StructuredBuffer<BbvOptionalData>		BitmaskBrickVoxelOptionData;
RWStructuredBuffer<BbvOptionalData>	RWBitmaskBrickVoxelOptionData;
RWBuffer<uint>    RWBbvRadianceAccumBuffer;
// 深度テストベース更新用のBrickリスト. 1..N に候補 Brick index+1 を格納し、0 は無効値.
Buffer<uint>		FrustumBrickList;
RWBuffer<uint>		RWFrustumBrickList;

// FSP cell-addressed resources use one global address space:
// cascade.cell_offset + X-major physical local index.
Buffer<uint>                          FspCellProbeIndexBuffer;
RWBuffer<uint>                        RWFspCellProbeIndexBuffer;
StructuredBuffer<FspProbePoolData>    FspProbePoolBuffer;
RWStructuredBuffer<FspProbePoolData>  RWFspProbePoolBuffer;
Buffer<uint>                          FspProbeFreeStack;
RWBuffer<uint>                        RWFspProbeFreeStack;
// ActiveProbeListは他のcounter付きappend bufferと異なり、先頭2ワードを世代交代counterに使用する。
// ワード2以降がProbe index listであり、Current/Previousのcounter slotは物理Bufferの世代ごとに交互利用する。
Buffer<uint>                          FspActiveProbeListPrev;
RWBuffer<uint>                        RWFspActiveProbeListPrev;
Buffer<uint>                          FspActiveProbeListCurr;
RWBuffer<uint>                        RWFspActiveProbeListCurr;
// SurfaceMask path: Cascadeごとの8x8x8 Brick内16ワードへ並べ替えた一時検出マスク。
Buffer<uint>                          FspSurfaceCellMaskBuffer;
RWBuffer<uint>                        RWFspSurfaceCellMaskBuffer;

void FspInjectCellMaskWave(bool has_cell, uint word_index, uint bit_mask)
{
    uint4 pending_lanes = WaveActiveBallot(has_cell);
    while(ballot_any(pending_lanes))
    {
        const uint leader_lane = first_lane_from_ballot(pending_lanes);
        const uint leader_word_index = WaveReadLaneAt(word_index, leader_lane);
        const bool is_same_word = has_cell && (word_index == leader_word_index);
        const uint4 same_word_lanes = WaveActiveBallot(is_same_word);
        const uint merged_mask = WaveActiveBitOr(is_same_word ? bit_mask : 0u);
        if(WaveGetLaneIndex() == leader_lane)
        {
            InterlockedOr(RWFspSurfaceCellMaskBuffer[leader_word_index], merged_mask);
        }
        pending_lanes &= ~same_word_lanes;
    }
}

// FSP update multipass (request/trace/resolve) 用のワークバッファ群。
Buffer<uint>                          FspProbeRayRequestBuffer;
RWBuffer<uint>                        RWFspProbeRayRequestBuffer;
Buffer<uint>                          FspProbeTraceIndirectArg;
RWBuffer<uint>                        RWFspProbeTraceIndirectArg;
Buffer<uint>                          FspProbeRayResultBuffer;
RWBuffer<uint>                        RWFspProbeRayResultBuffer;

Texture2D<float4>      FspProbeAtlasTex;
RWTexture2D<float4>    RWFspProbeAtlasTex;
StructuredBuffer<float4>      FspIrradianceVolumeSHBuffer;
RWStructuredBuffer<float4>    RWFspIrradianceVolumeSHBuffer;

// 0番目はアトミックカウンタ, それ以降はFSP X-major global cell index.
Buffer<uint>		SurfaceProbeCellList;
RWBuffer<uint>		RWSurfaceProbeCellList;

// instant_rdvのメインパラメータ.
ConstantBuffer<InstantRdvParam> cb_instant_rdv;

static const uint k_fsp_active_probe_counter_slot_count = 2u;
static const uint k_fsp_active_probe_list_data_offset = 2u;

uint FspActiveProbeCurrentCounterSlot()
{
    // frame_countはDispatch_Beginで1から始まる。各物理BufferがCurrentになるたびにslotを交互利用する。
    return ((cb_instant_rdv.frame_count - 1u) >> 1u) &
           (k_fsp_active_probe_counter_slot_count - 1u);
}

uint FspActiveProbePreviousCounterSlot()
{
    // 初回フレームのPreviousは初期化済みcounter slot 0を読む。
    if(cb_instant_rdv.frame_count <= 1u)
    {
        return 0u;
    }
    return ((cb_instant_rdv.frame_count - 2u) >> 1u) &
           (k_fsp_active_probe_counter_slot_count - 1u);
}

uint FspActiveProbeListAddress(uint list_index)
{
    return list_index + k_fsp_active_probe_list_data_offset;
}

uint BbvPhysicalVoxelCoordToMortonIndex(int3 coord, int3 resolution);
int3 BbvMortonIndexToPhysicalVoxelCoord(uint index, int3 resolution);
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution);

uint FspCascadeCount()
{
    return min((uint)cb_instant_rdv.fsp_cascade_count, k_fsp_max_cascade_count);
}

FspCascadeGridParam FspGetCascadeParam(uint cascade_index)
{
    return cb_instant_rdv.fsp_cascade[min(cascade_index, k_fsp_max_cascade_count - 1u)];
}

uint FspEncodeGlobalCellIndex(uint cascade_index, uint local_cell_index)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    return cascade.cell_offset + local_cell_index;
}

bool FspDecodeGlobalCellIndex(uint global_cell_index, out uint cascade_index, out uint local_cell_index)
{
    // CPU初期化時に全cascadeのcell_count一致と連続offsetを検証している。
    // この前提によりcascade配列scanを行わず、除算1回でglobal indexを分解できる。
    const uint cascade_count = FspCascadeCount();
    const uint cell_count_per_cascade = cb_instant_rdv.fsp_cascade[0].cell_count;
    if(cell_count_per_cascade == 0u || global_cell_index >= (uint)cb_instant_rdv.fsp_total_cell_count)
    {
        cascade_index = 0;
        local_cell_index = 0;
        return false;
    }

    cascade_index = global_cell_index / cell_count_per_cascade;
    local_cell_index = global_cell_index - cascade_index * cell_count_per_cascade;
    return cascade_index < cascade_count;
}

uint FspPhysicalCellCoordToLocalIndex(int3 physical_coord, int3 grid_resolution);
int3 FspLocalCellIndexToPhysicalCoord(uint local_cell_index, int3 grid_resolution);

uint FspSurfaceMaskWordsPerCascade()
{
    return (uint)max(
        cb_instant_rdv.fsp_surface_mask_words_per_cascade,
        0);
}

uint FspSurfaceMaskWordCount()
{
    return (uint)max(
        cb_instant_rdv.fsp_surface_mask_word_count,
        0);
}

bool FspGetSurfaceMaskAddressFromCell(
    uint cascade_index,
    int3 cell_coord,
    out uint word_index,
    out uint bit_mask)
{
    word_index = 0u;
    bit_mask = 0u;
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    if(any(cell_coord < 0) || any(cell_coord >= cascade.grid.grid_resolution))
    {
        return false;
    }

    const uint brick_axis =
        (uint)max(cb_instant_rdv.fsp_surface_mask_brick_axis, 0);
    const uint3 brick_coord = uint3(cell_coord) / k_fsp_surface_mask_brick_resolution;
    const uint brick_index =
        brick_coord.x +
        brick_coord.y * brick_axis +
        brick_coord.z * brick_axis * brick_axis;
    const uint local_cell_linear =
        (uint(cell_coord.x) % k_fsp_surface_mask_brick_resolution) +
        (uint(cell_coord.y) % k_fsp_surface_mask_brick_resolution) * k_fsp_surface_mask_brick_resolution +
        (uint(cell_coord.z) % k_fsp_surface_mask_brick_resolution) * k_fsp_surface_mask_brick_resolution * k_fsp_surface_mask_brick_resolution;

    word_index = cascade_index * FspSurfaceMaskWordsPerCascade() +
        brick_index * k_fsp_surface_mask_brick_word_count +
        local_cell_linear / 32u;
    bit_mask = 1u << (local_cell_linear & 31u);
    return true;
}

bool FspGetGlobalCellIndexFromSurfaceMaskBit(
    uint word_index,
    uint bit_index,
    out uint global_cell_index)
{
    global_cell_index = k_fsp_invalid_probe_index;
    const uint words_per_cascade = FspSurfaceMaskWordsPerCascade();
    const uint cascade_index = word_index / words_per_cascade;
    if(cascade_index >= FspCascadeCount())
    {
        return false;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const uint relative_word_index = word_index - cascade_index * words_per_cascade;
    const uint brick_index = relative_word_index / k_fsp_surface_mask_brick_word_count;
    const uint local_word_index = relative_word_index % k_fsp_surface_mask_brick_word_count;
    const uint brick_axis =
        (uint)max(cb_instant_rdv.fsp_surface_mask_brick_axis, 0);
    const uint3 brick_coord = uint3(
        brick_index % brick_axis,
        (brick_index / brick_axis) % brick_axis,
        brick_index / (brick_axis * brick_axis));
    const uint local_cell_linear = local_word_index * 32u + bit_index;
    const uint3 local_coord = uint3(
        local_cell_linear & 7u,
        (local_cell_linear >> 3u) & 7u,
        local_cell_linear >> 6u);
    const uint3 cell_coord = brick_coord * k_fsp_surface_mask_brick_resolution + local_coord;
    if(any(cell_coord >= uint3(cascade.grid.grid_resolution)))
    {
        return false;
    }

    const uint local_cell_index =
        cell_coord.x +
        cell_coord.y * uint(cascade.grid.grid_resolution.x) +
        cell_coord.z * uint(cascade.grid.grid_resolution.x) * uint(cascade.grid.grid_resolution.y);
    global_cell_index = cascade.cell_offset + local_cell_index;
    return true;
}

// FSP multipass request/result の packed key ヘルパー.
// [31:8] probe index, [7:0] oct cell index
static const uint k_fsp_ray_request_oct_cell_bits = 8u;
static const uint k_fsp_ray_request_oct_cell_mask = (1u << k_fsp_ray_request_oct_cell_bits) - 1u;
uint FspPackRayRequestKey(uint probe_index, uint oct_cell_index)
{
    return (probe_index << k_fsp_ray_request_oct_cell_bits) | (oct_cell_index & k_fsp_ray_request_oct_cell_mask);
}
uint FspUnpackRayRequestProbeIndex(uint packed_key)
{
    return (packed_key >> k_fsp_ray_request_oct_cell_bits);
}
uint FspUnpackRayRequestOctCellIndex(uint packed_key)
{
    return (packed_key & k_fsp_ray_request_oct_cell_mask);
}

// FSPの全cell-addressed resourceで共有するX-major local index。
// ActiveProbe lifecycle、SurfaceMask、IrradianceVolumeは必ずこのcodecを使う。
// BBVだけはray traversalの空間局所性を優先して、下部のMorton codecを使い続ける。
uint FspPhysicalCellCoordToLocalIndex(int3 physical_coord, int3 grid_resolution)
{
    return uint(physical_coord.x + physical_coord.y * grid_resolution.x +
        physical_coord.z * grid_resolution.x * grid_resolution.y);
}

int3 FspLocalCellIndexToPhysicalCoord(uint local_cell_index, int3 grid_resolution)
{
    const uint slice_cell_count = uint(grid_resolution.x * grid_resolution.y);
    const uint z = local_cell_index / slice_cell_count;
    const uint slice_index = local_cell_index - z * slice_cell_count;
    const uint y = slice_index / uint(grid_resolution.x);
    const uint x = slice_index - y * uint(grid_resolution.x);
    return int3(x, y, z);
}

int3 FspLocalCellIndexToLinearCoord(uint local_cell_index, InstantRdvToroidalGridParam grid)
{
    const int3 voxel_coord_toroidal =
        FspLocalCellIndexToPhysicalCoord(local_cell_index, grid.grid_resolution);
    return voxel_coord_toroidal_mapping(voxel_coord_toroidal, grid.grid_resolution - grid.grid_toroidal_offset, grid.grid_resolution);
}

float3 FspCalcCellCenterWs(uint cascade_index, uint local_cell_index)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const int3 voxel_coord = FspLocalCellIndexToLinearCoord(local_cell_index, cascade.grid);
    return (float3(voxel_coord) + 0.5) * cascade.grid.cell_size + cascade.grid.grid_min_pos;
}

bool FspTryGetGlobalCellIndexFromWorldPos(float3 pos_ws, uint cascade_index, out uint global_cell_index)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 voxel_coordf = (pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(all(voxel_coord >= 0) && all(voxel_coord < cascade.grid.grid_resolution))
    {
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cascade.grid.grid_toroidal_offset, cascade.grid.grid_resolution);
        const uint local_cell_index =
            FspPhysicalCellCoordToLocalIndex(voxel_coord_toroidal, cascade.grid.grid_resolution);
        global_cell_index = cascade.cell_offset + local_cell_index;
        return true;
    }

    global_cell_index = k_fsp_invalid_probe_index;
    return false;
}


bool FspTryGetFinestCascadePhysicalCellFromWorldPos(
    float3 pos_ws,
    out uint cascade_index,
    out int3 physical_cell_coord)
{
    const uint cascade_count = FspCascadeCount();
    [loop]
    for(uint ci = 0u; ci < cascade_count; ++ci)
    {
        const FspCascadeGridParam cascade = FspGetCascadeParam(ci);
        const int3 voxel_coord = floor(
            (pos_ws - cascade.grid.grid_min_pos) * cascade.grid.cell_size_inv);
        if(all(voxel_coord >= 0) && all(voxel_coord < cascade.grid.grid_resolution))
        {
            cascade_index = ci;
            physical_cell_coord = voxel_coord_toroidal_mapping(
                voxel_coord,
                cascade.grid.grid_toroidal_offset,
                cascade.grid.grid_resolution);
            return true;
        }
    }

    cascade_index = 0u;
    physical_cell_coord = 0.xxx;
    return false;
}


// Surface ownerのCell indexとMask addressを同じ座標変換から生成する。
// 境界帯では隣接coarse cascadeも返す。
uint FspGetSurfaceOwnerCellData(
    float3 pos_ws,
    out uint2 global_cell_indices,
    out uint2 word_indices,
    out uint2 bit_masks)
{
    global_cell_indices = k_fsp_invalid_probe_index.xx;
    word_indices = 0u.xx;
    bit_masks = 0u.xx;

    uint owner_cascade_index = 0u;
    int3 owner_physical_cell_coord = 0.xxx;
    if(!FspTryGetFinestCascadePhysicalCellFromWorldPos(
        pos_ws,
        owner_cascade_index,
        owner_physical_cell_coord))
    {
        return 0u;
    }

    const FspCascadeGridParam owner_cascade =
        FspGetCascadeParam(owner_cascade_index);
    global_cell_indices.x =
        owner_cascade.cell_offset +
        FspPhysicalCellCoordToLocalIndex(
            owner_physical_cell_coord,
            owner_cascade.grid.grid_resolution);
    if(!FspGetSurfaceMaskAddressFromCell(
        owner_cascade_index,
        owner_physical_cell_coord,
        word_indices.x,
        bit_masks.x))
    {
        return 0u;
    }

    const uint coarse_cascade_index = owner_cascade_index + 1u;
    if(coarse_cascade_index >= FspCascadeCount())
    {
        return 1u;
    }

    const FspCascadeGridParam coarse_cascade =
        FspGetCascadeParam(coarse_cascade_index);
    const float3 owner_cascade_max_pos =
        owner_cascade.grid.grid_min_pos +
        float3(owner_cascade.grid.grid_resolution) *
            owner_cascade.grid.cell_size;
    const float3 dist_to_min =
        pos_ws - owner_cascade.grid.grid_min_pos;
    const float3 dist_to_max =
        owner_cascade_max_pos - pos_ws;
    const float boundary_dist = min(
        min(dist_to_min.x, dist_to_max.x),
        min(
            min(dist_to_min.y, dist_to_max.y),
            min(dist_to_min.z, dist_to_max.z)));
    const float dither_width = max(
        coarse_cascade.grid.cell_size,
        owner_cascade.grid.cell_size);
    if(boundary_dist >= dither_width)
    {
        return 1u;
    }

    const int3 coarse_linear_cell_coord = floor(
        (pos_ws - coarse_cascade.grid.grid_min_pos) *
        coarse_cascade.grid.cell_size_inv);
    if(any(coarse_linear_cell_coord < 0) ||
       any(coarse_linear_cell_coord >=
           coarse_cascade.grid.grid_resolution))
    {
        return 1u;
    }

    const int3 coarse_physical_cell_coord =
        voxel_coord_toroidal_mapping(
            coarse_linear_cell_coord,
            coarse_cascade.grid.grid_toroidal_offset,
            coarse_cascade.grid.grid_resolution);
    global_cell_indices.y =
        coarse_cascade.cell_offset +
        FspPhysicalCellCoordToLocalIndex(
            coarse_physical_cell_coord,
            coarse_cascade.grid.grid_resolution);
    if(!FspGetSurfaceMaskAddressFromCell(
        coarse_cascade_index,
        coarse_physical_cell_coord,
        word_indices.y,
        bit_masks.y))
    {
        global_cell_indices.y = k_fsp_invalid_probe_index;
        return 1u;
    }

    return 2u;
}

uint FspGetSurfaceOwnerMaskAddresses(
    float3 pos_ws,
    out uint2 word_indices,
    out uint2 bit_masks)
{
    uint2 global_cell_indices = k_fsp_invalid_probe_index.xx;
    return FspGetSurfaceOwnerCellData(
        pos_ws,
        global_cell_indices,
        word_indices,
        bit_masks);
}

uint2 FspProbeAtlasMapPos(uint probe_index)
{
    return uint2(probe_index % cb_instant_rdv.fsp_probe_atlas_tile_width, probe_index / cb_instant_rdv.fsp_probe_atlas_tile_width);
}

uint2 FspProbeAtlasTexelCoord(uint probe_index, uint2 oct_cell_id)
{
    return FspProbeAtlasMapPos(probe_index) * k_fsp_probe_octmap_width + oct_cell_id;
}

int3 FspIrradianceVolumeToroidalPhysicalCoord(int3 linear_coord, InstantRdvToroidalGridParam grid)
{
    // FSP IVはCPU初期化時に各軸同一かつ2冪と検証する。これが崩れると `& (N - 1)` はmoduloにならず、
    // 誤ったcell参照や範囲外addressの原因になるため、任意解像度対応時はこの関数も同時に変更すること。
    return (linear_coord + grid.grid_toroidal_offset) & (grid.grid_resolution - 1);
}


uint FspIrradianceVolumeCellIndexFromLinearCoord(uint cascade_index, int3 linear_coord)
{
    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const int3 physical_coord = FspIrradianceVolumeToroidalPhysicalCoord(linear_coord, cascade.grid);
    return cascade.cell_offset + FspPhysicalCellCoordToLocalIndex(physical_coord, cascade.grid.grid_resolution);
}

uint FspIrradianceVolumeSHAddress(uint irradiance_volume_cell_index, uint coeff_index)
{
    return irradiance_volume_cell_index * k_fsp_irradiance_volume_sh_float4_count + coeff_index;
}

float4 FspIrradianceVolumeLoadCoeff(uint irradiance_volume_cell_index, uint coeff_index)
{
    return FspIrradianceVolumeSHBuffer[FspIrradianceVolumeSHAddress(irradiance_volume_cell_index, coeff_index)];
}

bool FspIrradianceVolumeHasValidSH(uint irradiance_volume_cell_index)
{
    const float4 coeff0 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 0);
    const float4 coeff1 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 1);
    const float4 coeff2 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 2);
    const float4 coeff3 = FspIrradianceVolumeLoadCoeff(irradiance_volume_cell_index, 3);
    return any(abs(coeff0) > 0.0.xxxx) ||
        any(abs(coeff1) > 0.0.xxxx) ||
        any(abs(coeff2) > 0.0.xxxx) ||
        any(abs(coeff3) > 0.0.xxxx);
}

bool FspIrradianceVolumeHasValidSHCoeff(float4 coeff0, float4 coeff1, float4 coeff2, float4 coeff3)
{
    return any(abs(coeff0) > 0.0.xxxx) ||
        any(abs(coeff1) > 0.0.xxxx) ||
        any(abs(coeff2) > 0.0.xxxx) ||
        any(abs(coeff3) > 0.0.xxxx);
}

bool FspTryGetActiveProbeForCell(out uint out_probe_index, out FspProbePoolData out_probe_pool_data, uint global_cell_index)
{
    out_probe_index = k_fsp_invalid_probe_index;
    out_probe_pool_data = (FspProbePoolData)0;

    if(global_cell_index >= (uint)cb_instant_rdv.fsp_total_cell_count)
    {
        return false;
    }

    const uint probe_index = FspCellProbeIndexBuffer[global_cell_index];
    if(probe_index == k_fsp_invalid_probe_index || probe_index >= (uint)cb_instant_rdv.fsp_probe_pool_size)
    {
        return false;
    }

    const FspProbePoolData probe_pool_data = FspProbePoolBuffer[probe_index];
    if(probe_pool_data.owner_cell_index != global_cell_index)
    {
        return false;
    }

    out_probe_index = probe_index;
    out_probe_pool_data = probe_pool_data;
    return true;
}

bool FspIsActiveProbeOwnedCell(uint global_cell_index)
{
    uint probe_index = k_fsp_invalid_probe_index;
    FspProbePoolData probe_pool_data = (FspProbePoolData)0;
    return FspTryGetActiveProbeForCell(probe_index, probe_pool_data, global_cell_index);
}


// ------------------------------------------------------------------------------------------------------------------------
bool isValidDepth(float d)
{
    // 深度が有効範囲内かどうかを判定.
    return (0.0 < d && d < 1.0);
}

float2 SspCalcPrevFrameUvFromWorldPos(float3 pos_ws, float3x4 prev_view_mtx, float4x4 prev_proj_mtx, out bool is_valid)
{
    const float3 prev_pos_vs = mul(prev_view_mtx, float4(pos_ws, 1.0)).xyz;
    const float4 prev_pos_cs = mul(prev_proj_mtx, float4(prev_pos_vs, 1.0));
    if(abs(prev_pos_cs.w) <= 1e-6)
    {
        is_valid = false;
        return float2(0.0, 0.0);
    }

    const float2 prev_ndc_xy = prev_pos_cs.xy / prev_pos_cs.w;
    const float2 prev_uv = float2(prev_ndc_xy.x * 0.5 + 0.5, -prev_ndc_xy.y * 0.5 + 0.5);
    is_valid = all(prev_uv >= 0.0) && all(prev_uv <= 1.0);
    return prev_uv;
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
// 球面/半球切り替え用. ワールド空間方向->OctahedralMapエンコード.
float2 SspEncodeDirByNormal(float3 dir_ws, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralEncodeHemisphereDirWs(dir_ws, base_normal_ws);
    #else
        return OctahedralEncodeSphereDirWs(dir_ws);
    #endif
}
// OctahedralMapストレージ向け.
// 球面/半球切り替え用. UV空間方向->OctahedralMapデコード. 内部で接空間構築するバージョン.
float3 SspDecodeDirByNormal(float2 oct_uv, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralDecodeHemisphereDirWs(oct_uv, base_normal_ws);
    #else
        return OctahedralDecodeSphereDirWs(oct_uv);
    #endif
}
// OctahedralMapストレージ向け.
// 球面/半球切り替え用. UV空間方向->OctahedralMapデコード.
float3 SspDecodeDirByNormal(float2 oct_uv, float3 basis_t_ws, float3 basis_b_ws, float3 base_normal_ws)
{
    #if NGL_SSP_OCTAHEDRALMAP_STORAGE_HEMISPHERE_MODE
        return OctahedralDecodeHemisphereDirWs(oct_uv, basis_t_ws, basis_b_ws, base_normal_ws);
    #else
        return OctahedralDecodeSphereDirWs(oct_uv);
    #endif
}


// BBV専用Morton codec。ray traversal時の空間局所性を維持するため、BBVだけはZ-orderを使う。
// 0..N^3-1を隙間なくMorton indexとして扱うには、BBV解像度がcubic power-of-twoである必要がある。
// ActiveProbe/SurfaceMask/IrradianceVolumeはFSP X-major codecを使い、この関数を呼んではならない。
uint BbvPhysicalVoxelCoordToMortonIndex(int3 coord, int3 resolution)
{
    return EncodeMortonCodeX10Y10Z10(coord);
}

int3 BbvMortonIndexToPhysicalVoxelCoord(uint index, int3 resolution)
{
    return DecodeMortonCodeX10Y10Z10(index);
}


// リニアなVoxel座標をループするToroidalマッピングに変換する.
//  ToroidalMapping座標をリニア座標に戻す変換は
//      voxel_coord_toroidal_mapping(voxel_coord_toroidal, cb_instant_rdv.bbv.grid_resolution - cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_resolution)
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
    return cb_instant_rdv.bbv.grid_resolution.x * cb_instant_rdv.bbv.grid_resolution.y * cb_instant_rdv.bbv.grid_resolution.z;
}
// BBV本体バッファは [bitmask region][brick data region] の順。
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

#if NGL_INSTANT_RDV_ENABLE_BRICK_LOCAL_AABB
// BrickLocalAABB の packed min/max アドレス.
uint bbv_voxel_brick_local_aabb_min_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 2;
}
uint bbv_voxel_brick_local_aabb_max_addr(uint voxel_index)
{
    return bbv_voxel_unique_data_addr(voxel_index) + 3;
}
#endif
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

uint bbv_radiance_accum_addr_base(uint voxel_index)
{
    return voxel_index * k_bbv_radiance_accum_component_count;
}
uint bbv_radiance_accum_r_addr(uint voxel_index)
{
    return bbv_radiance_accum_addr_base(voxel_index) + 0;
}
uint bbv_radiance_accum_g_addr(uint voxel_index)
{
    return bbv_radiance_accum_addr_base(voxel_index) + 1;
}
uint bbv_radiance_accum_b_addr(uint voxel_index)
{
    return bbv_radiance_accum_addr_base(voxel_index) + 2;
}
uint bbv_radiance_accum_count_addr(uint voxel_index)
{
    return bbv_radiance_accum_addr_base(voxel_index) + 3;
}

int2 bbv_radiance_injection_tile_grid_resolution(int2 src_resolution)
{
    return (src_resolution + (k_bbv_radiance_injection_tile_width - 1)) /
        k_bbv_radiance_injection_tile_width;
}

int2 bbv_radiance_injection_group_grid_resolution(int2 src_resolution)
{
    // odd tile 数端も取りこぼさないよう切り上げで group 数を求める。
    return (bbv_radiance_injection_tile_grid_resolution(src_resolution) + (k_bbv_radiance_injection_tile_group_resolution - 1)) / k_bbv_radiance_injection_tile_group_resolution;
}

bool bbv_radiance_injection_group_coord_to_tile_coord(int2 group_coord, out int2 tile_coord, int2 src_resolution)
{
    const uint tile_phase =
        cb_instant_rdv.frame_count % k_bbv_radiance_injection_phase_count;
    const int2 local_phase = int2(
        tile_phase & 1,
        (tile_phase >> 1) & 1);
    tile_coord = group_coord * k_bbv_radiance_injection_tile_group_resolution + local_phase;

    // 端の不完全 group だけ範囲外 tile が出るので、そこだけ無効化する。
    return all(tile_coord < bbv_radiance_injection_tile_grid_resolution(src_resolution));
}

int3 bbv_radiance_resolve_group_grid_resolution()
{
    // odd 解像度端も取りこぼさないよう切り上げで group 数を求める。
    return (cb_instant_rdv.bbv.grid_resolution + (k_bbv_radiance_resolve_brick_group_resolution - 1)) / k_bbv_radiance_resolve_brick_group_resolution;
}

uint bbv_radiance_resolve_dispatch_count()
{
    // 1 dispatch = 1 group。2x2x2 内のどの Brick を処理するかは frame_count の下位 3bit で決める。
    const int3 group_grid_resolution = bbv_radiance_resolve_group_grid_resolution();
    return group_grid_resolution.x * group_grid_resolution.y * group_grid_resolution.z;
}

bool bbv_radiance_resolve_dispatch_index_to_voxel_coord(uint dispatch_index, out int3 voxel_coord)
{
    const int3 group_grid_resolution = bbv_radiance_resolve_group_grid_resolution();
    const int3 group_coord = BbvMortonIndexToPhysicalVoxelCoord(dispatch_index, group_grid_resolution);
    // frame_count 下位 3bit を 2x2x2 group 内 local xyz へ割り当てて、8F で group 内全 Brick を巡回する。
    const uint phase = cb_instant_rdv.frame_count & (k_bbv_radiance_resolve_phase_count - 1);
    const int3 local_phase = int3(
        phase & 1,
        (phase >> 1) & 1,
        (phase >> 2) & 1);
    voxel_coord = group_coord * k_bbv_radiance_resolve_brick_group_resolution + local_phase;

    // 端の不完全 group だけ範囲外が出るので、そこだけ無効化する。
    return all(voxel_coord < cb_instant_rdv.bbv.grid_resolution);
}

// Brick 内の occupied voxel count を、簡易 cone 積分で使う 0..1 の密度近似へ変換する。
// この段階では空間分布は見ず、8x8x8 内の充填率だけを使う。
float bbv_brick_occupancy_ratio_from_count(uint occupied_voxel_count)
{
    return saturate(float(occupied_voxel_count) / float(k_bbv_per_voxel_bitmask_bit_count));
}

uint calc_bbv_subbrick_index(uint3 subbrick_coord)
{
    return subbrick_coord.x
        + (subbrick_coord.y * k_bbv_subbrick_per_voxel_axis_count)
        + (subbrick_coord.z * (k_bbv_subbrick_per_voxel_axis_count * k_bbv_subbrick_per_voxel_axis_count));
}

uint3 calc_bbv_subbrick_coord_from_index(uint subbrick_index)
{
    return uint3(
        subbrick_index % k_bbv_subbrick_per_voxel_axis_count,
        (subbrick_index / k_bbv_subbrick_per_voxel_axis_count) % k_bbv_subbrick_per_voxel_axis_count,
        subbrick_index / (k_bbv_subbrick_per_voxel_axis_count * k_bbv_subbrick_per_voxel_axis_count));
}

uint calc_bbv_subbrick_linear_bitcell_index(uint3 subbrick_local_pos)
{
    return subbrick_local_pos.x
        + (subbrick_local_pos.y * k_bbv_subbrick_resolution)
        + (subbrick_local_pos.z * (k_bbv_subbrick_resolution * k_bbv_subbrick_resolution));
}

uint3 calc_bbv_subbrick_local_pos_from_linear_bitcell_index(uint bit_index)
{
    return uint3(
        bit_index % k_bbv_subbrick_resolution,
        (bit_index / k_bbv_subbrick_resolution) % k_bbv_subbrick_resolution,
        bit_index / (k_bbv_subbrick_resolution * k_bbv_subbrick_resolution));
}

uint calc_bbv_subbrick_morton_bitcell_index(uint3 subbrick_local_pos)
{
    return ((subbrick_local_pos.x >> 0) & 1u) << 0
        | (((subbrick_local_pos.y >> 0) & 1u) << 1)
        | (((subbrick_local_pos.z >> 0) & 1u) << 2)
        | (((subbrick_local_pos.x >> 1) & 1u) << 3)
        | (((subbrick_local_pos.y >> 1) & 1u) << 4)
        | (((subbrick_local_pos.z >> 1) & 1u) << 5);
}

uint3 calc_bbv_subbrick_local_pos_from_morton_bitcell_index(uint bit_index)
{
    return uint3(
        ((bit_index >> 0) & 1u) | (((bit_index >> 3) & 1u) << 1),
        ((bit_index >> 1) & 1u) | (((bit_index >> 4) & 1u) << 1),
        ((bit_index >> 2) & 1u) | (((bit_index >> 5) & 1u) << 1));
}

uint calc_bbv_subbrick_bitcell_index(uint3 subbrick_local_pos)
{
#if (NGL_INSTANT_RDV_BBV_SUBBRICK_BIT_LAYOUT == NGL_INSTANT_RDV_BBV_SUBBRICK_BIT_LAYOUT_MORTON)
    return calc_bbv_subbrick_morton_bitcell_index(subbrick_local_pos);
#else
    return calc_bbv_subbrick_linear_bitcell_index(subbrick_local_pos);
#endif
}

uint3 calc_bbv_subbrick_local_pos_from_bitcell_index(uint bit_index)
{
#if (NGL_INSTANT_RDV_BBV_SUBBRICK_BIT_LAYOUT == NGL_INSTANT_RDV_BBV_SUBBRICK_BIT_LAYOUT_MORTON)
    return calc_bbv_subbrick_local_pos_from_morton_bitcell_index(bit_index);
#else
    return calc_bbv_subbrick_local_pos_from_linear_bitcell_index(bit_index);
#endif
}

// Bbvの内部座標を元に Brick 内 bitmask インデックスを計算.
// Brick は 2x2x2 個の 4x4x4 SubBrick に分け、各 SubBrick 内 64bit を linear / Morton で切り替える。
uint calc_bbv_bitcell_index(uint3 bitcell_pos)
{
    const uint3 subbrick_coord = bitcell_pos / k_bbv_subbrick_resolution;
    const uint3 subbrick_local_pos = bitcell_pos % k_bbv_subbrick_resolution;
    return calc_bbv_subbrick_index(subbrick_coord) * k_bbv_subbrick_bit_count
        + calc_bbv_subbrick_bitcell_index(subbrick_local_pos);
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
    const uint bitcell_index = calc_bbv_bitcell_index(bitcell_pos);

    calc_bbv_bitcell_info_from_bitcell_index(out_u32_offset, out_bit_location, bitcell_index);
}

// Bbvのビットセルインデックスから k_bbv_per_voxel_resolution^3 ボクセル内位置を計算.
// bit_index : 0 〜 k_bbv_per_voxel_bitmask_bit_count-1
uint3 calc_bbv_bitcell_pos_from_bit_index(uint bit_index)
{
    const uint subbrick_index = bit_index / k_bbv_subbrick_bit_count;
    const uint subbrick_local_bit_index = bit_index % k_bbv_subbrick_bit_count;
    const uint3 subbrick_coord = calc_bbv_subbrick_coord_from_index(subbrick_index);
    const uint3 subbrick_local_pos = calc_bbv_subbrick_local_pos_from_bitcell_index(subbrick_local_bit_index);
    return subbrick_coord * k_bbv_subbrick_resolution + subbrick_local_pos;
}

#if NGL_INSTANT_RDV_ENABLE_BRICK_LOCAL_AABB
uint bbv_pack_brick_local_aabb_coord(int3 local_coord)
{
    return uint(local_coord.x) | (uint(local_coord.y) << 3) | (uint(local_coord.z) << 6);
}
int3 bbv_unpack_brick_local_aabb_coord(uint packed_coord)
{
    return int3(
        int(packed_coord & 0x7u),
        int((packed_coord >> 3) & 0x7u),
        int((packed_coord >> 6) & 0x7u));
}
void bbv_load_brick_local_aabb(
    out int3 out_local_coord_min,
    out int3 out_local_coord_max_exclusive,
    Buffer<uint> bbv_buffer,
    uint voxel_index)
{
    out_local_coord_min = bbv_unpack_brick_local_aabb_coord(bbv_buffer[bbv_voxel_brick_local_aabb_min_addr(voxel_index)]);
    out_local_coord_max_exclusive = bbv_unpack_brick_local_aabb_coord(bbv_buffer[bbv_voxel_brick_local_aabb_max_addr(voxel_index)]) + 1;
}
#endif


// ------------------------------------------------------------------------------------------------------------------------
// BbvのBrickデータレイアウト.

// uint[0]      : Brick内 occupied voxel count.
// uint[1]      : 旧最終可視フレーム用の予約ワード。現在はクリア以外に使用しない。
#if NGL_INSTANT_RDV_ENABLE_BRICK_LOCAL_AABB
// uint[2]      : BrickLocalAABB min (packed xyz, 3bit each).
// uint[3]      : BrickLocalAABB max inclusive (packed xyz, 3bit each).
#endif


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
        const uint voxel_index = BbvPhysicalVoxelCoordToMortonIndex(voxel_coord_toroidal, grid_resolution);

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
        int3 trace_coord_min, int3 trace_coord_max,
        Buffer<uint> bbv_buffer, uint bbv_bitmask_addr,
        
        const bool intersection_bit_mode, // Bbvの占有状態のどちらと交差をするか指定する. true:通常通り占有されたVoxelと交差, false:非占有Voxelと交差.

        const bool static_enable_initial_hit_avoidance,
        int initial_hit_avoidance_count,

        const bool is_brick_mode // ヒットをVoxelではなくBrickで完了させるモード. Brickの占有フラグのデバッグ用.
    ) 
{
    rayPos = clamp(rayPos, float3(trace_coord_min) + 0.0001, float3(trace_coord_max) - 0.0001);

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
            
        } while (all(mapPos >= trace_coord_min) && all(mapPos < trace_coord_max));

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

// BBV trace用の逆方向成分。軸並行に近い成分は正の1e6へ置き換える。
// 境界挙動を維持するため、単純な逆数計算への変更は別途検証する。
float3 calc_safe_trace_ray_dir_inv(float3 ray_dir)
{
    // 軸並行の問題の対処。OctahedralMapセル中心レイ等で発生しやすいため対処。
    const float3 inv_ray_dir = 1.0 / ray_dir;
    const float3 safe_inv_ray_dir = select(abs(ray_dir) < 1e-6, float3(1e6, 1e6, 1e6), inv_ray_dir);
    return safe_inv_ray_dir;

}

// レイ上の現在 t 位置から、次のセル内側を確実にサンプルするための t を計算.
float calc_trace_sample_t(float curr_t, float end_t)
{
    const float k_trace_t_epsilon = 1e-4;
    return min(curr_t + k_trace_t_epsilon, max(curr_t, end_t - k_trace_t_epsilon));
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
        const uint voxel_index = BbvPhysicalVoxelCoordToMortonIndex(toroidal_map_pos, grid_resolution);
        inout_brick_check_count++;
        const bool bbv_occupied_flag = (0 != bbv_buffer[bbv_voxel_coarse_occupancy_info_addr(voxel_index)]);
        if(!intersection_bit_mode || bbv_occupied_flag)
        {
            float detail_begin_t = brick_begin_t;
            float detail_end_t = brick_end_t;
            int3 trace_coord_min = int3(0, 0, 0);
            int3 trace_coord_max = int3(k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution, k_bbv_per_voxel_resolution);
            bool should_trace_detail = true;
#if NGL_INSTANT_RDV_ENABLE_BRICK_LOCAL_AABB
            if(intersection_bit_mode && bbv_occupied_flag && !is_brick_mode)
            {
                int3 local_coord_min;
                int3 local_coord_max_exclusive;
                bbv_load_brick_local_aabb(local_coord_min, local_coord_max_exclusive, bbv_buffer, voxel_index);

                const float3 local_aabb_min = float3(map_pos) + float3(local_coord_min) * k_bbv_per_voxel_resolution_inv;
                const float3 local_aabb_max = float3(map_pos) + float3(local_coord_max_exclusive) * k_bbv_per_voxel_resolution_inv;
                if(!calc_ray_t_offset_for_aabb(detail_begin_t, detail_end_t, local_aabb_min, local_aabb_max, clampled_start_pos, ray_dir_ws, ray_dir_inv, brick_end_t))
                {
                    should_trace_detail = false;
                }
                if(should_trace_detail)
                {
                    detail_begin_t = max(detail_begin_t, brick_begin_t);
                    detail_end_t = min(detail_end_t, brick_end_t);
                    if(detail_end_t < detail_begin_t)
                    {
                        should_trace_detail = false;
                    }
                }

                trace_coord_min = local_coord_min;
                trace_coord_max = local_coord_max_exclusive;
            }
#endif
            if(should_trace_detail)
            {
                const float3 brick_entry_pos = clampled_start_pos + ray_dir_ws * detail_begin_t;
                const float3 pos_in_brick = (brick_entry_pos - map_pos) * k_bbv_per_voxel_resolution;

                inout_bitmask_check_count++;
                bool3 detail_step_mask = bool3(false, false, false);
                const int3 sub_map_pos = trace_bitmask_brick(
                    pos_in_brick,
                    ray_dir_ws,
                    ray_dir_sign,
                    ray_dir_inv,
                    detail_step_mask,
                    trace_coord_min,
                    trace_coord_max,
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

    out_hit_voxel_index = BbvPhysicalVoxelCoordToMortonIndex(
        voxel_coord_toroidal_mapping(hit_map_pos, bbv_grid_toroidal_offset, grid_resolution),
        grid_resolution);
    const float3 hit_normal = select(hit_step_mask, -ray_dir_sign, 0.0);
    const float hit_t_ws = (hit_t + ray_trace_begin_t_offset) * cell_width_ws;
    return float4(hit_t_ws, hit_normal.x, hit_normal.y, hit_normal.z);
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
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Fsp.
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


#endif // NGL_SHADER_INSTANT_RDV_UTIL_H

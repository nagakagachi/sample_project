/*
    vsp_surface_detect_reduced_cs.hlsl

    ReducedSurfaceBufferからVSP SurfaceCellを検出する。

    ActiveProbeの配置にはCell indexだけでなく、そのCellをActivateした
    実Surfaceの位置と法線が必要になる。そのprovenanceを失わないよう、
    Cellを初めて検出したthreadがCell indexと検出元Reduced texelを
    2つの並行リストの同一slotへ出力する。

    後段のVspPreUpdateはこのtexelからSurface anchorを再構成し、
    ActiveProbeをSurface法線の表面側へRelocationする。
*/

#define VSP_SURFACE_DETECT_TILE_WIDTH 8
#define VSP_SURFACE_DETECT_TILE_HEIGHT 8

#include "../instant_rdv_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D<float4> TexReducedSurfaceBuffer;
RWBuffer<uint> RWSurfaceProbeSourceTexelList;

void VspDetectSurfaceCellWave(
    bool has_cell,
    uint global_cell_index,
    uint mask_word_index,
    uint mask_bit,
    uint reduced_surface_texel_index)
{
    const uint capacity =
        (uint)max(cb_instant_rdv.vsp_visible_voxel_buffer_size, 0);
    uint4 pending_lanes = WaveActiveBallot(has_cell);
    while(ballot_any(pending_lanes))
    {
        const uint leader_lane =
            first_lane_from_ballot(pending_lanes);
        const uint leader_word_index =
            WaveReadLaneAt(mask_word_index, leader_lane);
        const bool is_same_word =
            has_cell && mask_word_index == leader_word_index;
        const uint4 same_word_lanes =
            WaveActiveBallot(is_same_word);
        const uint merged_mask = WaveActiveBitOr(
            is_same_word ? mask_bit : 0u);

        uint original_mask = 0u;
        if(WaveGetLaneIndex() == leader_lane)
        {
            InterlockedOr(
                RWVspSurfaceCellMaskBuffer[leader_word_index],
                merged_mask,
                original_mask);
        }
        original_mask =
            WaveReadLaneAt(original_mask, leader_lane);
        const uint new_mask = merged_mask & ~original_mask;
        const uint new_cell_count = countbits(new_mask);

        uint append_base_index = 0u;
        if(WaveGetLaneIndex() == leader_lane &&
           new_cell_count > 0u)
        {
            InterlockedAdd(
                RWSurfaceProbeCellList[0],
                new_cell_count,
                append_base_index);
            if(append_base_index + new_cell_count > capacity)
            {
                InterlockedMin(
                    RWSurfaceProbeCellList[0],
                    capacity);
            }
        }
        append_base_index =
            WaveReadLaneAt(append_base_index, leader_lane);

        const bool is_new_cell =
            is_same_word && ((new_mask & mask_bit) != 0u);
        uint4 pending_new_cell_lanes =
            WaveActiveBallot(is_new_cell);
        while(ballot_any(pending_new_cell_lanes))
        {
            const uint cell_leader_lane =
                first_lane_from_ballot(
                    pending_new_cell_lanes);
            const uint cell_bit = WaveReadLaneAt(
                mask_bit,
                cell_leader_lane);
            const bool is_same_cell =
                is_new_cell && mask_bit == cell_bit;
            const uint4 same_cell_lanes =
                WaveActiveBallot(is_same_cell);
            if(WaveGetLaneIndex() == cell_leader_lane)
            {
                const uint cell_rank =
                    countbits(new_mask & (cell_bit - 1u));
                const uint list_index =
                    append_base_index + cell_rank;
                if(list_index < capacity)
                {
                    // この同一slot対応がCellと検出元Surfaceのprovenance契約。
                    RWSurfaceProbeCellList[list_index + 1u] =
                        global_cell_index;
                    RWSurfaceProbeSourceTexelList[
                        list_index + 1u] =
                        reduced_surface_texel_index;
                }
            }
            pending_new_cell_lanes &= ~same_cell_lanes;
        }

        pending_lanes &= ~same_word_lanes;
    }
}

[numthreads(
    VSP_SURFACE_DETECT_TILE_WIDTH,
    VSP_SURFACE_DETECT_TILE_HEIGHT,
    1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    uint reduced_width = 0u;
    uint reduced_height = 0u;
    TexReducedSurfaceBuffer.GetDimensions(
        reduced_width,
        reduced_height);
    if(any(dtid.xy >= uint2(reduced_width, reduced_height)))
    {
        return;
    }

    const float4 surface_sample =
        TexReducedSurfaceBuffer.Load(int3(dtid.xy, 0));
    if(surface_sample.x <= 0.0)
    {
        return;
    }

    const uint2 source_resolution =
        uint2(cb_instant_rdv.tex_main_view_depth_size);
    const uint2 source_texel = ReducedSurfaceBufferSourceTexel(
        dtid.xy,
        source_resolution,
        cb_instant_rdv.frame_count);
    const float2 source_uv =
        (float2(source_texel) + 0.5.xx) /
        float2(source_resolution);
    const float3 surface_pos_vs = CalcViewSpacePosition(
        source_uv,
        surface_sample.x,
        cb_ngl_sceneview.cb_proj_mtx);
    const float3 surface_pos_ws = mul(
        cb_ngl_sceneview.cb_view_inv_mtx,
        float4(surface_pos_vs, 1.0));

    uint2 owner_cell_indices = k_vsp_invalid_probe_index.xx;
    uint2 owner_mask_word_indices = 0u.xx;
    uint2 owner_mask_bits = 0u.xx;
    const uint owner_count = VspGetSurfaceOwnerCellData(
        surface_pos_ws,
        owner_cell_indices,
        owner_mask_word_indices,
        owner_mask_bits);
    const uint reduced_surface_texel_index =
        dtid.x + dtid.y * reduced_width;
    VspDetectSurfaceCellWave(
        owner_count > 0u,
        owner_cell_indices.x,
        owner_mask_word_indices.x,
        owner_mask_bits.x,
        reduced_surface_texel_index);
    VspDetectSurfaceCellWave(
        owner_count > 1u,
        owner_cell_indices.y,
        owner_mask_word_indices.y,
        owner_mask_bits.y,
        reduced_surface_texel_index);
}

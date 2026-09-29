/*
    bbv_occupancy_injection_apply_integrated_surface_cs.hlsl
    BBV Occupancy InjectionとVSP SurfaceCell検出を統合したMainView用Injection。

    MainViewのDepth pixelでBBV占有を書き込み、同じパスで所有Cascadeの
    VSP SurfaceCellMaskをSurface owner cellへ限定してマーキングし、
    全Cascade走査を実行しない。
*/

#define TILE_WIDTH 8

#include "../instant_rdv_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

// BBVのDepth Injection元View情報。
ConstantBuffer<BbvSurfaceInjectionViewInfo> cb_injection_src_view_info;

// ShadowViewやDepth Atlasを含むInjection元のハードウェア深度。
Texture2D TexHardwareDepth;

// BBV占有とSurface owner cell限定のVSP SurfaceCell検出を同じDepth走査で実行する。
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    // Depth Atlasの対象範囲外は、BBVとVSPのどちらにも書き込まない。
    if(any(dtid.xy >= cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw))
    {
        return;
    }

    const float d = TexHardwareDepth.Load(int3(
        dtid.xy + cb_injection_src_view_info.cb_view_depth_buffer_offset_size.xy, 0)).r;
    const bool has_surface = 0.0 < d && d < 1.0;
    float3 surface_pos_ws = 0.0.xxx;
    float3 injection_pos_ws = 0.0.xxx;
    if(has_surface)
    {
        const float2 screen_uv =
            (float2(dtid.xy) + 0.5.xx) /
            float2(cb_injection_src_view_info.cb_view_depth_buffer_offset_size.zw);
        const float view_z = calc_view_z_from_ndc_z(
            d, cb_injection_src_view_info.cb_ndc_z_to_view_z_coef);
        const float3 pixel_pos_vs = CalcViewSpacePosition(
            screen_uv, view_z, cb_injection_src_view_info.cb_proj_mtx);

        // 表面位置はVSPと同じDepth復元値を使い、逆ビュー変換を一度だけ行う。
        surface_pos_ws = mul(
            cb_injection_src_view_info.cb_view_inv_mtx,
            float4(pixel_pos_vs, 1.0));

        // BBV側だけを視線奥へずらし、Injection直後のRemovalによる消失を抑える。
        const float3 view_ray_origin_vs = CalcViewSpacePosition(
            screen_uv,
            cb_injection_src_view_info.cb_near_plane_view_z,
            cb_injection_src_view_info.cb_proj_mtx);
        const float3 to_pixel_vec_vs = pixel_pos_vs - view_ray_origin_vs;
        const float inv_to_pixel_len =
            rsqrt(max(dot(to_pixel_vec_vs, to_pixel_vec_vs), 1e-10));
        const float3 view_ray_ws = mul(
            cb_injection_src_view_info.cb_view_inv_mtx,
            float4(to_pixel_vec_vs * inv_to_pixel_len, 0.0));
        injection_pos_ws = surface_pos_ws +
            view_ray_ws * cb_instant_rdv.bbv_occupancy_injection_world_offset;
    }

    // Injection位置をBBVのBrick/FineVoxel座標へ変換する。
    bool has_injection = false;
    uint voxel_index = 0u;
    uint bitmask_u32_offset = 0u;
    uint bitmask_append = 0u;
    const float3 voxel_coordf =
        (injection_pos_ws - cb_instant_rdv.bbv.grid_min_pos) *
        cb_instant_rdv.bbv.cell_size_inv;
    const int3 voxel_coord = floor(voxel_coordf);
    if(has_surface && all(voxel_coord >= 0) && all(voxel_coord < cb_instant_rdv.bbv.grid_resolution))
    {
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(
            voxel_coord,
            cb_instant_rdv.bbv.grid_toroidal_offset,
            cb_instant_rdv.bbv.grid_resolution);
        voxel_index = BbvPhysicalVoxelCoordToMortonIndex(
            voxel_coord_toroidal, cb_instant_rdv.bbv.grid_resolution);
        const float3 voxel_coord_frac = frac(voxel_coordf);
        const uint3 voxel_coord_bitmask_pos =
            uint3(voxel_coord_frac * k_bbv_per_voxel_resolution);
        uint bitcell_u32_bit_pos;
        calc_bbv_bitcell_info(bitmask_u32_offset, bitcell_u32_bit_pos, voxel_coord_bitmask_pos);
        bitmask_append = 1u << bitcell_u32_bit_pos;
        has_injection = true;
    }

    // 同一Wave内で同じBBV bitmask wordを共有するpixelを集約する。
    uint4 pending_lanes = WaveActiveBallot(has_injection);
    while(ballot_any(pending_lanes))
    {
        const uint leader_lane = first_lane_from_ballot(pending_lanes);
        const uint leader_voxel = WaveReadLaneAt(voxel_index, leader_lane);
        const uint leader_u32_offset = WaveReadLaneAt(bitmask_u32_offset, leader_lane);
        const bool is_same_target = has_injection &&
            voxel_index == leader_voxel &&
            bitmask_u32_offset == leader_u32_offset;
        const uint4 same_target_lanes = WaveActiveBallot(is_same_target);
        const uint merged_append = WaveActiveBitOr(is_same_target ? bitmask_append : 0u);
        if(WaveGetLaneIndex() == leader_lane)
        {
            const uint bbv_addr = bbv_voxel_bitmask_data_addr(leader_voxel);
            InterlockedOr(RWBitmaskBrickVoxel[bbv_addr + leader_u32_offset], merged_append);

        }
        pending_lanes &= ~same_target_lanes;
    }

    // VSP Surface候補はMainViewだけから生成する。
    if(cb_injection_src_view_info.cb_is_main_view == 0 ||
       !has_surface)
    {
        return;
    }

    // Surface owner cellとして最細Cascadeと境界帯の隣接Cascadeだけを対象にする。
    uint2 owner_mask_words = 0u.xx;
    uint2 owner_mask_bits = 0u.xx;
    const uint owner_count = VspGetSurfaceOwnerMaskAddresses(
        surface_pos_ws,
        owner_mask_words,
        owner_mask_bits);
    VspInjectCellMaskWave(owner_count > 0u, owner_mask_words.x, owner_mask_bits.x);
    VspInjectCellMaskWave(owner_count > 1u, owner_mask_words.y, owner_mask_bits.y);
}

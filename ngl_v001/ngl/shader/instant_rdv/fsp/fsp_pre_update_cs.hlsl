
#if 0

fsp_pre_update_cs.hlsl

可視SurfaceProbeリストを元に、probe 割り当てと配置調整を行う.

#endif


#include "../instant_rdv_util.hlsli"
#include "../../include/scene_view_struct.hlsli"



ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D<float4> TexReducedSurfaceBuffer;
Buffer<uint> SurfaceProbeSourceTexelList;

// SurfaceCell検出時に保存したReduced texelから、そのCellをActivateした
// 実Surfaceの位置と法線を復元する。Cell中心からの再探索は行わない。
// Cell/sourceリストの同一slot対応はFSP Reduced経路のprovenance契約。
bool FspTryGetReducedSurfaceAnchor(
    uint update_element_index,
    uint global_cell_index,
    uint cascade_index,
    out float3 surface_pos_ws,
    out float3 surface_normal_ws)
{
    surface_pos_ws = 0.0.xxx;
    surface_normal_ws = 0.0.xxx;
    if(cb_instant_rdv.main_view_reduced_surface_enable == 0)
    {
        return false;
    }

    uint reduced_width = 0;
    uint reduced_height = 0;
    TexReducedSurfaceBuffer.GetDimensions(reduced_width, reduced_height);
    if(reduced_width == 0 || reduced_height == 0)
    {
        return false;
    }

    const uint2 source_resolution =
        uint2(cb_instant_rdv.tex_main_view_depth_size);
    if(any(source_resolution == 0u.xx))
    {
        return false;
    }

    const uint reduced_surface_texel_index =
        SurfaceProbeSourceTexelList[update_element_index + 1u];
    if(reduced_surface_texel_index >= reduced_width * reduced_height)
    {
        return false;
    }

    const uint2 reduced_texel = uint2(
        reduced_surface_texel_index % reduced_width,
        reduced_surface_texel_index / reduced_width);
    const float4 surface_sample =
        TexReducedSurfaceBuffer.Load(int3(reduced_texel, 0));
    if(surface_sample.x <= 0.0)
    {
        return false;
    }

    const uint2 source_texel = ReducedSurfaceBufferSourceTexel(
        reduced_texel,
        source_resolution,
        cb_instant_rdv.frame_count);
    const float2 source_uv =
        (float2(source_texel) + 0.5.xx) /
        float2(source_resolution);
    const float3 surface_pos_vs = CalcViewSpacePosition(
        source_uv,
        surface_sample.x,
        cb_ngl_sceneview.cb_proj_mtx);
    surface_pos_ws = mul(
        cb_ngl_sceneview.cb_view_inv_mtx,
        float4(surface_pos_vs, 1.0));

    uint sample_global_cell_index = k_fsp_invalid_probe_index;
    if(!FspTryGetGlobalCellIndexFromWorldPos(
            surface_pos_ws,
            cascade_index,
            sample_global_cell_index) ||
       sample_global_cell_index != global_cell_index)
    {
        return false;
    }

    surface_normal_ws = normalize(OctDecode(surface_sample.yz));
    return true;
}

// free stack から 1 probe index を pop する。
uint FspPopFreeProbeIndex()
{
    for(;;)
    {
        const uint observed_count = RWFspProbeFreeStack[0];
        if(observed_count == 0)
        {
            return k_fsp_invalid_probe_index;
        }

        uint cas_old_value = 0;
        InterlockedCompareExchange(RWFspProbeFreeStack[0], observed_count, observed_count - 1, cas_old_value);
        if(cas_old_value == observed_count)
        {
            return RWFspProbeFreeStack[observed_count];
        }
    }
}

// 現フレーム active list へ probe を追加する。
void FspPushCurrActiveProbeIndex(uint probe_index)
{
    uint active_list_index = 0;
    InterlockedAdd(
        RWFspActiveProbeListCurr[FspActiveProbeCurrentCounterSlot()],
        1,
        active_list_index);
    if(active_list_index < cb_instant_rdv.fsp_active_probe_buffer_size)
    {
        RWFspActiveProbeListCurr[FspActiveProbeListAddress(active_list_index)] = probe_index;
    }
}

// probe atlas 1 tile を明示的にゼロ初期化する。
void FspClearProbeAtlas(uint probe_index)
{
    const uint2 probe_2d_map_pos = FspProbeAtlasMapPos(probe_index);
    [unroll]
    for(int oct_j = 0; oct_j < k_fsp_probe_octmap_width; ++oct_j)
    {
        [unroll]
        for(int oct_i = 0; oct_i < k_fsp_probe_octmap_width; ++oct_i)
        {
            RWFspProbeAtlasTex[probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)] = 0.0.xxxx;
        }
    }
}

// 既存 probe の atlas 内容を新規 probe へコピーする。
void FspCopyProbeAtlas(uint dst_probe_index, uint src_probe_index)
{
    const uint2 dst_probe_2d_map_pos = FspProbeAtlasMapPos(dst_probe_index);
    const uint2 src_probe_2d_map_pos = FspProbeAtlasMapPos(src_probe_index);
    [unroll]
    for(int oct_j = 0; oct_j < k_fsp_probe_octmap_width; ++oct_j)
    {
        [unroll]
        for(int oct_i = 0; oct_i < k_fsp_probe_octmap_width; ++oct_i)
        {
            RWFspProbeAtlasTex[dst_probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)] =
                RWFspProbeAtlasTex[src_probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)];
        }
    }
}

bool FspTrySeedProbeFromNearestActiveProbe(
    float3 sample_pos_ws,
    uint dst_cascade_index,
    uint dst_probe_index)
{
    const uint cascade_count = FspCascadeCount();
    [loop]
    for(uint cascade_offset = 0; cascade_offset < cascade_count - dst_cascade_index; ++cascade_offset)
    {
        const uint src_cascade_index = dst_cascade_index + cascade_offset;
        const FspCascadeGridParam src_cascade = FspGetCascadeParam(src_cascade_index);
        const float3 continuous_coord =
            (sample_pos_ws - src_cascade.grid.grid_min_pos) *
            src_cascade.grid.cell_size_inv - 0.5.xxx;
        const int3 center_coord = int3(floor(continuous_coord));
        const int3 resolution = src_cascade.grid.grid_resolution;
        uint best_probe_index = k_fsp_invalid_probe_index;
        float best_distance_sq = 3.402823466e+38;

        [loop]
        for(int z = -1; z <= 1; ++z)
        {
            for(int y = -1; y <= 1; ++y)
            {
                for(int x = -1; x <= 1; ++x)
                {
                    const int3 logical_coord = center_coord + int3(x, y, z);
                    if(any(logical_coord < 0) || any(logical_coord >= resolution))
                    {
                        continue;
                    }

                    const int3 physical_coord = voxel_coord_toroidal_mapping(
                        logical_coord,
                        src_cascade.grid.grid_toroidal_offset,
                        resolution);
                    const uint local_cell_index = FspPhysicalCellCoordToLocalIndex(
                        physical_coord,
                        resolution);
                    const uint src_global_cell_index =
                        FspEncodeGlobalCellIndex(src_cascade_index, local_cell_index);
                    const uint src_probe_index = RWFspCellProbeIndexBuffer[src_global_cell_index];
                    if(src_probe_index == k_fsp_invalid_probe_index ||
                       src_probe_index >= (uint)cb_instant_rdv.fsp_probe_pool_size ||
                       src_probe_index == dst_probe_index)
                    {
                        continue;
                    }

                    const FspProbePoolData src_probe_pool_data =
                        RWFspProbePoolBuffer[src_probe_index];
                    if(src_probe_pool_data.owner_cell_index != src_global_cell_index ||
                       src_probe_pool_data.last_update_frame == 0)
                    {
                        continue;
                    }

                    const float3 source_position_ws =
                        FspCalcCellCenterWs(src_cascade_index, local_cell_index) +
                        decode_uint_to_range1_vec3(src_probe_pool_data.probe_offset_v3) *
                        (src_cascade.grid.cell_size *
                         cb_instant_rdv.fsp_relocation_offset_scale_for_cascade_cell_size);
                    const float3 delta = source_position_ws - sample_pos_ws;
                    const float distance_sq = dot(delta, delta);
                    if(distance_sq < best_distance_sq)
                    {
                        best_distance_sq = distance_sq;
                        best_probe_index = src_probe_index;
                    }
                }
            }
        }

        if(best_probe_index != k_fsp_invalid_probe_index)
        {
            FspCopyProbeAtlas(dst_probe_index, best_probe_index);
            return true;
        }
    }

    return false;
}

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // SurfaceProbeCellListを利用するバージョン.
    const uint visible_voxel_count = SurfaceProbeCellList[0]; // 0番目にアトミックカウンタが入っている.
    const uint update_element_index = dtid.x;
    
    if(visible_voxel_count <= update_element_index)
        return;

    const uint global_cell_index = SurfaceProbeCellList[update_element_index+1]; // 1番目以降に有効Cellインデックスが入っている.
    uint cascade_index = 0;
    uint local_cell_index = 0;
    if(!FspDecodeGlobalCellIndex(global_cell_index, cascade_index, local_cell_index))
    {
        return;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const float3 probe_cell_center =
        FspCalcCellCenterWs(cascade_index, local_cell_index);
    const float relocation_offset_limit =
        cascade.grid.cell_size *
        cb_instant_rdv.fsp_relocation_offset_scale_for_cascade_cell_size;
    float3 reduced_relocation_pos_ws = 0.0.xxx;
    if(cb_instant_rdv.main_view_reduced_surface_enable != 0)
    {
        // BBVはsolid volumeではなくSurface奥側の薄い占有層なので、
        // 非占有判定だけではSurfaceの表裏を決定できない。
        // Activate起点のSurface anchorから法線方向へ配置することで、
        // ActiveProbeが床下などSurface裏側へRelocationすることを防ぐ。
        float3 surface_pos_ws = 0.0.xxx;
        float3 surface_normal_ws = 0.0.xxx;
        if(!FspTryGetReducedSurfaceAnchor(
            update_element_index,
            global_cell_index,
            cascade_index,
            surface_pos_ws,
            surface_normal_ws))
        {
            return;
        }

        const float surface_clearance =
            max(
                cb_instant_rdv.bbv_occupancy_injection_world_offset,
                cb_instant_rdv.bbv.cell_size /
                    float(k_bbv_per_voxel_resolution));
        const float clearance_scale[4] = {1.0, 2.0, 3.0, 4.0};
        bool found_relocation = false;
        [unroll]
        for(int candidate_index = 0; candidate_index < 4; ++candidate_index)
        {
            const float3 candidate_ws =
                surface_pos_ws +
                surface_normal_ws *
                    (surface_clearance *
                     clearance_scale[candidate_index]);
            const float3 candidate_offset =
                candidate_ws - probe_cell_center;
            if(any(abs(candidate_offset) > relocation_offset_limit.xxx))
            {
                continue;
            }
            if(read_bbv_voxel_from_world_pos(
                    BitmaskBrickVoxel,
                    cb_instant_rdv.bbv.grid_resolution,
                    cb_instant_rdv.bbv.grid_toroidal_offset,
                    cb_instant_rdv.bbv.grid_min_pos,
                    cb_instant_rdv.bbv.cell_size_inv,
                    candidate_ws) != 0)
            {
                continue;
            }

            reduced_relocation_pos_ws = candidate_ws;
            found_relocation = true;
            break;
        }
        if(!found_relocation)
        {
            return;
        }
    }

    uint probe_index = RWFspCellProbeIndexBuffer[global_cell_index];
    const bool is_probe_lifecycle_enabled = (0 != cb_instant_rdv.fsp_probe_lifecycle_enable);
    bool is_new_probe = false;
    if(k_fsp_invalid_probe_index == probe_index)
    {
        if(!is_probe_lifecycle_enabled)
        {
            return;
        }
        probe_index = FspPopFreeProbeIndex();
        if(k_fsp_invalid_probe_index == probe_index)
        {
            return;
        }
        RWFspCellProbeIndexBuffer[global_cell_index] = probe_index;
        FspPushCurrActiveProbeIndex(probe_index);
        is_new_probe = true;
    }

    FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[probe_index];
    if(!is_probe_lifecycle_enabled)
    {
        probe_pool_data.last_seen_frame = cb_instant_rdv.frame_count;
        RWFspProbePoolBuffer[probe_index] = probe_pool_data;

        const uint owner_cell_index = probe_pool_data.owner_cell_index;
        return;
    }
    if(is_new_probe)
    {
        probe_pool_data.probe_offset_v3 = 0;
        probe_pool_data.last_update_frame = 0;
    }
    probe_pool_data.owner_cell_index = global_cell_index;
    probe_pool_data.last_seen_frame = cb_instant_rdv.frame_count;
    probe_pool_data.reserved0 = 0;
    probe_pool_data.reserved1 = 0;
    probe_pool_data.reserved2 = 0;

    // Probe埋まり回避.
    float3 probe_sample_pos_ws = reduced_relocation_pos_ws;
    if(cb_instant_rdv.main_view_reduced_surface_enable == 0)
    {
        const float3 view_origin =
            GetViewOriginFromInverseViewMatrix(
                cb_ngl_sceneview.cb_view_inv_mtx);
        const float3 prev_probe_offset =
            decode_uint_to_range1_vec3(
                probe_pool_data.probe_offset_v3) *
            relocation_offset_limit;
        probe_sample_pos_ws =
            probe_cell_center + prev_probe_offset;
        const float3 to_camera_vec =
            view_origin - probe_cell_center;
        const float to_camera_len_sq =
            dot(to_camera_vec, to_camera_vec);
        if(to_camera_len_sq > 1e-6)
        {
            const float3 dir_to_camera_ws =
                to_camera_vec * rsqrt(to_camera_len_sq);
            const float front_samples[6] =
                {0.95, 0.80, 0.65, 0.50, 0.35, 0.20};
            [unroll]
            for(int sample_index = 0;
                sample_index < 6;
                ++sample_index)
            {
                const float3 candidate_ws =
                    probe_cell_center +
                    dir_to_camera_ws *
                        (relocation_offset_limit *
                         front_samples[sample_index]);
                if(read_bbv_voxel_from_world_pos(
                        BitmaskBrickVoxel,
                        cb_instant_rdv.bbv.grid_resolution,
                        cb_instant_rdv.bbv.grid_toroidal_offset,
                        cb_instant_rdv.bbv.grid_min_pos,
                        cb_instant_rdv.bbv.cell_size_inv,
                        candidate_ws) == 0)
                {
                    probe_sample_pos_ws = candidate_ws;
                    break;
                }
            }
        }

        const int fallback_relocation_count = 4;
        bool found_non_solid = false;

        if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_instant_rdv.bbv.grid_resolution, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size_inv, probe_sample_pos_ws) != 0)
        {
            // 深度ヒント方向を優先して、セル手前側から空き位置を探す.
            const float seed_len_sq = dot(prev_probe_offset, prev_probe_offset);
            const float3 preferred_dir = (seed_len_sq > 1e-6) ? (prev_probe_offset * rsqrt(seed_len_sq)) : 0.0.xxx;
            if(any(preferred_dir != 0.0.xxx))
            {
                const float front_bias_samples[4] = {0.95, 0.75, 0.55, 0.35};
                [unroll]
                for(int fi = 0; fi < 4; ++fi)
                {
                    probe_sample_pos_ws = probe_cell_center + preferred_dir * (relocation_offset_limit * front_bias_samples[fi]);
                    if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_instant_rdv.bbv.grid_resolution, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size_inv, probe_sample_pos_ws) == 0)
                    {
                        found_non_solid = true;
                        break;
                    }
                }
            }

            // 方向優先で見つからないときだけランダム探索へフォールバック.
            // シードはセルID固定にして、静止時のフレーム間ゆらぎを抑える。
            if(!found_non_solid)
            {
                for(int ri = 0; ri < fallback_relocation_count; ++ri)
                {
                    const uint seed_0 = (global_cell_index * 1664525u + (ri + 1u) * 1013904223u);
                    // update_element_index(=visible list順序) 依存を避け、セルIDベースで安定化.
                    const uint seed_1 = seed_0 ^ (global_cell_index * 2246822519u + 3266489917u);
                    const uint seed_2 = seed_0 ^ ((global_cell_index + 1u) * 668265263u + 374761393u);
                    const float3 random_offset = float3(
                        noise_float_to_float(float2(global_cell_index, seed_0)),
                        noise_float_to_float(float2(seed_1, global_cell_index)),
                        noise_float_to_float(float2(seed_2, seed_0))) - 0.5;
                    probe_sample_pos_ws = probe_cell_center + random_offset * (cascade.grid.cell_size * 0.4);// レンジはセルを超えない程度.

                    if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_instant_rdv.bbv.grid_resolution, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size_inv, probe_sample_pos_ws) == 0)
                    {
                        found_non_solid = true;
                        break;
                    }
                }
            }
        }
    }
    // 保存前に必ずエンコード可能範囲へ正規化してクランプする。
    const float3 normalized_probe_offset = clamp((probe_sample_pos_ws - probe_cell_center) / relocation_offset_limit, -1.0.xxx, 1.0.xxx);
    probe_sample_pos_ws = probe_cell_center + normalized_probe_offset * relocation_offset_limit;
    const uint encoded_probe_offset = encode_range1_vec3_to_uint(normalized_probe_offset);
    // セルサイズと再配置スケールの積で正規化したオフセットを保存する。
    probe_pool_data.probe_offset_v3 = encoded_probe_offset;

    if(is_new_probe)
    {
        if(cb_instant_rdv.fsp_warm_start_enable != 0)
        {
            if(!FspTrySeedProbeFromNearestActiveProbe(
                probe_sample_pos_ws,
                cascade_index,
                probe_index))
            {
                // source が見つからない場合だけ新規割り当て時に明示的に初期化する。
                FspClearProbeAtlas(probe_index);
            }
        }
        else
        {
            FspClearProbeAtlas(probe_index);
        }
    }

    RWFspProbePoolBuffer[probe_index] = probe_pool_data;
}

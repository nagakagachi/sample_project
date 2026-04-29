
#if 0

fsp_pre_update_cs.hlsl

可視SurfaceProbeリストを元に、probe 割り当てと配置調整を行う.

#endif


#include "../srvs_util.hlsli"


#define RAY_SAMPLE_COUNT_PER_VOXEL 8
#define PROBE_UPDATE_TEMPORAL_RATE (0.025)

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
    const uint update_element_index = (dtid.x * (FSP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1) + (cb_srvs.frame_count%(FSP_VISIBLE_SURFACE_ELEMENT_UPDATE_SKIP_COUNT+1)));
    
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
    uint probe_index = RWFspCellProbeIndexBuffer[global_cell_index];
    if(k_fsp_invalid_probe_index == probe_index)
    {
        probe_index = FspPopFreeProbeIndex();
        if(k_fsp_invalid_probe_index == probe_index)
        {
            return;
        }
        RWFspCellProbeIndexBuffer[global_cell_index] = probe_index;
    }

    FspProbePoolData probe_pool_data = RWFspProbePoolBuffer[probe_index];
    probe_pool_data.owner_cell_index = global_cell_index;
    probe_pool_data.last_seen_frame = cb_srvs.frame_count;
    probe_pool_data.debug_last_observed_frame = cb_srvs.frame_count;
    probe_pool_data.flags |= (k_fsp_probe_flag_allocated | k_fsp_probe_flag_visible_this_frame);

    // Cell中心.
    const float3 probe_cell_center = FspCalcCellCenterWs(cascade_index, local_cell_index);
    #if 1
        // Probe埋まり回避.
        // RWFspProbeBuffer[voxel_index].probe_offset_v3のuintからfloat3オフセット復元. Cellサイズの半分で正規化.
        float3 prev_probe_offset = decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * (cascade.grid.cell_size * 0.5);
        float3 probe_sample_pos_ws = probe_cell_center + prev_probe_offset;

        {
            const int relocation_count = 8;

            if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_srvs.bbv.grid_resolution, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size_inv, probe_sample_pos_ws) != 0)
            {
                // 最大relocation_count回だけ埋まり回避のオフセットを試行する.
                for(int ri = 0; ri < relocation_count; ++ri)
                {
                    // Voxel内ランダムオフセットを加える.
                    const uint seed_0 = cb_srvs.frame_count + ri;
                    const float3 random_offset = float3(noise_float_to_float(float2(global_cell_index, seed_0)), noise_float_to_float(float2(update_element_index, seed_0)), noise_float_to_float(float2(seed_0, global_cell_index))) - 0.5;
                    probe_sample_pos_ws = probe_cell_center + random_offset * (cascade.grid.cell_size * 0.4);// レンジはセルを超えない程度.

                    if(read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_srvs.bbv.grid_resolution, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size_inv, probe_sample_pos_ws) == 0)
                    {
                        break;
                    }
                }
            }
        }
        // Probe位置更新. Cellサイズの半分で正規化.
        probe_pool_data.probe_offset_v3 = encode_range1_vec3_to_uint(((probe_sample_pos_ws - probe_cell_center) / (cascade.grid.cell_size * 0.5)));
    #else
        // 埋まり回避なし
        float3 probe_sample_pos_ws = probe_cell_center;
    #endif

    // 既存 debug / scratch path 互換のため、セル側 legacy buffer にも最低限ミラーしておく。
    RWFspProbeBuffer[global_cell_index].probe_offset_v3 = probe_pool_data.probe_offset_v3;
    RWFspProbeBuffer[global_cell_index].avg_sky_visibility = probe_pool_data.avg_sky_visibility;
    RWFspProbePoolBuffer[probe_index] = probe_pool_data;

    #if 0
        // プローブデータの整理のため一旦ここでの書き込みは無効化. 全域更新のほうで検証中.
        /*
        // Probeレイサンプル.
        {
            for(int sample_index = 0; sample_index < RAY_SAMPLE_COUNT_PER_VOXEL; ++sample_index)
            {
                // 全域Probe更新.
                #if 1
                    // 球面Fibonacciシーケンス分布上をフルでトレースする.
                    // 同時更新されるProbeのレイ方向がほとんど同じになるためか, Probe毎に乱数でサンプルするよりも数倍速くなる模様.
                    const int num_fibonacci_point_max = 128;
                    float3 sample_ray_dir = fibonacci_sphere_point((cb_srvs.frame_count*RAY_SAMPLE_COUNT_PER_VOXEL + sample_index)%num_fibonacci_point_max, num_fibonacci_point_max);
                #else
                    // Probe毎にランダムな方向をサンプリングする.
                    float3 sample_ray_dir = random_unit_vector3( float2( cb_srvs.frame_count + sample_index, update_element_index + sample_index * 37 ) );
                #endif

                const float3 sample_ray_origin = probe_sample_pos_ws;            
                    
                // SkyVisibility raycast.
                const float trace_distance = k_fsp_probe_distance_max;
                int hit_voxel_index = -1;
                    float4 debug_ray_info;
                // リファクタリング版.
#if NGL_SRVS_TRACE_USE_HIBRICK_FSP_VISIBLE_SURFACE_ELEMENT_UPDATE
                float4 curr_ray_t_ws = trace_bbv_hibrick(
                    hit_voxel_index, debug_ray_info,
                    sample_ray_origin, sample_ray_dir, trace_distance, 
                    cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                    cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);
#else
                float4 curr_ray_t_ws = trace_bbv(
                    hit_voxel_index, debug_ray_info,
                    sample_ray_origin, sample_ray_dir, trace_distance, 
                    cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                    cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel);
#endif

                // SkyVisibilityの方向平均を更新.
                const float distance_probe_value = (0.0 > curr_ray_t_ws.x) ? 1.0 : 0.0;

                    // ProbeOctMapの更新.
                const float2 octmap_uv = OctEncode(sample_ray_dir);
                const uint2 probe_2d_map_pos = FspProbeAtlasMapPos(probe_index);

                // 境界部込のテクセル位置.
                const uint2 octmap_atlas_texel_pos = probe_2d_map_pos * k_fsp_probe_octmap_width_with_border + 1 + clamp(uint2(octmap_uv * k_fsp_probe_octmap_width), 0, (k_fsp_probe_octmap_width - 1));
                RWFspProbeAtlasTex[octmap_atlas_texel_pos] = lerp(RWFspProbeAtlasTex[octmap_atlas_texel_pos], distance_probe_value, PROBE_UPDATE_TEMPORAL_RATE);
            }
        }
        */
    #endif
}

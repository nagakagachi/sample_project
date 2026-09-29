#if 0

vsp_probe_ray_trace_cs.hlsl

VSP update multipass の Trace パス。
1thread = 1request で BBV をトレースし、result を append する。
result[0] は atomic counter、result payload は [packed request key, hit info] の2uint。
Radiance 読み取りは Resolve パスへ分離して trace を軽量化する。

#endif

#include "../instant_rdv_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

#define VSP_RAY_LINEAR_THREAD_GROUP_SIZE 128u
#define VSP_RAY_RESULT_STRIDE 2u
#define VSP_RAY_RESULT_PACKED_REQUEST_KEY 0u
#define VSP_RAY_RESULT_HIT_INFO 1u

[numthreads(VSP_RAY_LINEAR_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 gtid : SV_GroupThreadID,
    uint gindex : SV_GroupIndex,
    uint3 gid : SV_GroupID)
{
    const uint request_linear_index = gid.x * VSP_RAY_LINEAR_THREAD_GROUP_SIZE + gindex;
    const uint total_request_count = VspProbeRayRequestBuffer[0];
    if(request_linear_index >= total_request_count)
    {
        return;
    }
    const uint packed_request_key = VspProbeRayRequestBuffer[1u + request_linear_index];
    const uint probe_index = VspUnpackRayRequestProbeIndex(packed_request_key);
    const uint oct_cell_index = VspUnpackRayRequestOctCellIndex(packed_request_key);

    bool emit_result = false;
    uint packed_hit_info = 0u; // 0 = sky
    if((probe_index < (uint)cb_instant_rdv.vsp_probe_pool_size) && (oct_cell_index < (k_vsp_probe_octmap_width * k_vsp_probe_octmap_width)))
    {
        const VspProbePoolData probe_pool_data = VspProbePoolBuffer[probe_index];
        if(probe_pool_data.owner_cell_index != k_vsp_invalid_probe_index)
        {
            uint cascade_index = 0u;
            uint local_cell_index = 0u;
            if(VspDecodeGlobalCellIndex(probe_pool_data.owner_cell_index, cascade_index, local_cell_index))
            {
                const VspCascadeGridParam cascade = VspGetCascadeParam(cascade_index);
                const float cascade_relocation_offset_normalize_distance =
                    (cascade.grid.cell_size * cb_instant_rdv.vsp_relocation_offset_scale_for_cascade_cell_size);
                const float3 probe_offset =
                    decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * cascade_relocation_offset_normalize_distance;
                const float3 probe_pos_ws = VspCalcCellCenterWs(cascade_index, local_cell_index) + probe_offset;

                const uint oct_x = oct_cell_index % k_vsp_probe_octmap_width;
                const uint oct_y = oct_cell_index / k_vsp_probe_octmap_width;
                float2 oct_uv = (float2(oct_x, oct_y) + 0.5.xx) / float(k_vsp_probe_octmap_width);
                if(0 != cb_instant_rdv.debug_vsp_update_ray_jitter_enable)
                {
                    // 1セル=1レイ対応は維持しつつ、セル内だけ時変ジッタする。
                    const float2 jitter01 = float2(
                        noise_float_to_float(float2(float(probe_index + cb_instant_rdv.frame_count * 131u), float(oct_cell_index))),
                        noise_float_to_float(float2(float(probe_index * 17u + cb_instant_rdv.frame_count * 73u), float(oct_cell_index * 13u + 7u))));
                    const float2 jitter = (jitter01 - 0.5.xx) / float(k_vsp_probe_octmap_width);
                    oct_uv = clamp(oct_uv + jitter, 1e-4.xx, (1.0 - 1e-4).xx);
                }

                const float3 sample_ray_dir = OctDecode(oct_uv);
                const float trace_distance = k_vsp_probe_distance_max;
                int hit_voxel_index = -1;
                float4 debug_ray_info = 0.0.xxxx;
                const float4 curr_ray_t_ws = trace_bbv(
                    hit_voxel_index, debug_ray_info,
                    probe_pos_ws, sample_ray_dir, trace_distance,
                    cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size, cb_instant_rdv.bbv.grid_resolution,
                    cb_instant_rdv.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

                const bool is_sky_visible = (curr_ray_t_ws.x < 0.0) || (hit_voxel_index < 0);
                packed_hit_info = is_sky_visible ? 0u : (uint(hit_voxel_index) + 1u);
                emit_result = true;
            }
        }
    }

    const uint4 ballot = WaveActiveBallot(emit_result);
    if(!ballot_any(ballot))
    {
        return;
    }

    const uint wave_emit_count = WaveActiveCountBits(emit_result);
    const uint wave_emit_prefix = WavePrefixCountBits(emit_result);
    const uint leader_lane = first_lane_from_ballot(ballot);
    uint wave_base_index = 0u;
    if(WaveGetLaneIndex() == leader_lane)
    {
        InterlockedAdd(RWVspProbeRayResultBuffer[0], wave_emit_count, wave_base_index);
    }
    wave_base_index = WaveReadLaneAt(wave_base_index, leader_lane);

    if(emit_result)
    {
        const uint result_index = wave_base_index + wave_emit_prefix;
        const uint result_word_offset = 1u + result_index * VSP_RAY_RESULT_STRIDE;
        RWVspProbeRayResultBuffer[result_word_offset + VSP_RAY_RESULT_PACKED_REQUEST_KEY] = packed_request_key;
        RWVspProbeRayResultBuffer[result_word_offset + VSP_RAY_RESULT_HIT_INFO] = packed_hit_info;
    }
}

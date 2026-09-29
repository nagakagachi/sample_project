#if 0

vsp_probe_ray_request_cs.hlsl

VSP update multipass の Request パス。
active probe ごとに oct cell request を append する。
request[0] は atomic counter、request[1..] は packed request key。
WaveIntrinsics で wave 内 prefix を取り、wave 代表 lane だけ atomic add する。

#endif

#include "../instant_rdv_util.hlsli"

#define VSP_PROBE_RAY_COUNT_PER_PROBE (k_vsp_probe_octmap_width * k_vsp_probe_octmap_width)
#define VSP_REQUEST_THREAD_GROUP_SIZE 128u

[numthreads(VSP_REQUEST_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint active_probe_count =
        VspActiveProbeListCurr[VspActiveProbeCurrentCounterSlot()];
    if(dtid.x >= active_probe_count)
    {
        return;
    }

    const uint probe_index =
        VspActiveProbeListCurr[VspActiveProbeListAddress(dtid.x)];
    const bool is_probe_index_valid = (probe_index < (uint)cb_instant_rdv.vsp_probe_pool_size);

    [unroll]
    for(uint oct_cell_index = 0u; oct_cell_index < VSP_PROBE_RAY_COUNT_PER_PROBE; ++oct_cell_index)
    {
        // 将来ここで request skip 判定を入れても append 方式のまま対応可能。
        const bool emit_request = is_probe_index_valid;
        const uint4 ballot = WaveActiveBallot(emit_request);
        if(!ballot_any(ballot))
        {
            continue;
        }

        const uint wave_emit_count = WaveActiveCountBits(emit_request);
        const uint wave_emit_prefix = WavePrefixCountBits(emit_request);
        const uint leader_lane = first_lane_from_ballot(ballot);

        uint wave_base_index = 0u;
        if(WaveGetLaneIndex() == leader_lane)
        {
            InterlockedAdd(RWVspProbeRayRequestBuffer[0], wave_emit_count, wave_base_index);
        }
        wave_base_index = WaveReadLaneAt(wave_base_index, leader_lane);

        if(emit_request)
        {
            const uint request_index = 1u + wave_base_index + wave_emit_prefix;
            RWVspProbeRayRequestBuffer[request_index] = VspPackRayRequestKey(probe_index, oct_cell_index);
        }
    }
}

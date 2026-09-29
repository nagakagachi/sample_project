#if 0

vsp_probe_ray_resolve_cs.hlsl

VSP update multipass の Resolve パス。
Trace パスが保存した hit voxel index を使って radiance を取得し、
ProbeAtlas を更新する。trace 側から radiance read/write を分離している。

#endif

#include "../instant_rdv_util.hlsli"

#define VSP_OCTA_UPDATE_TEMPORAL_RATE (0.95)
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
    const uint result_linear_index = gid.x * VSP_RAY_LINEAR_THREAD_GROUP_SIZE + gindex;
    const uint total_result_count = VspProbeRayResultBuffer[0];
    if(result_linear_index >= total_result_count)
    {
        return;
    }

    const uint result_word_offset = 1u + result_linear_index * VSP_RAY_RESULT_STRIDE;
    const uint packed_request_key = VspProbeRayResultBuffer[result_word_offset + VSP_RAY_RESULT_PACKED_REQUEST_KEY];
    const uint hit_voxel_index_plus_1 = VspProbeRayResultBuffer[result_word_offset + VSP_RAY_RESULT_HIT_INFO];

    const uint probe_index = VspUnpackRayRequestProbeIndex(packed_request_key);
    const uint oct_cell_index = VspUnpackRayRequestOctCellIndex(packed_request_key);
    if(probe_index >= (uint)cb_instant_rdv.vsp_probe_pool_size)
    {
        return;
    }
    if(oct_cell_index >= (k_vsp_probe_octmap_width * k_vsp_probe_octmap_width))
    {
        return;
    }

    VspProbePoolData probe_pool_data = RWVspProbePoolBuffer[probe_index];
    if(probe_pool_data.owner_cell_index == k_vsp_invalid_probe_index)
    {
        return;
    }

    const uint oct_x = oct_cell_index % k_vsp_probe_octmap_width;
    const uint oct_y = oct_cell_index / k_vsp_probe_octmap_width;
    const uint2 oct_cell_id = uint2(oct_x, oct_y);
    const bool is_sky_visible = (0u == hit_voxel_index_plus_1);
    const float sky_visibility = is_sky_visible ? 1.0 : 0.0;
    const float3 hit_radiance = is_sky_visible
        ? 0.0.xxx
        : max(BitmaskBrickVoxelOptionData[hit_voxel_index_plus_1 - 1u].resolved_radiance, 0.0.xxx);

    const uint2 atlas_texel_pos = VspProbeAtlasTexelCoord(probe_index, oct_cell_id);
    const float4 atlas_prev = RWVspProbeAtlasTex[atlas_texel_pos];
    const float4 atlas_curr = float4(hit_radiance, sky_visibility);
    RWVspProbeAtlasTex[atlas_texel_pos] = lerp(atlas_curr, atlas_prev, VSP_OCTA_UPDATE_TEMPORAL_RATE);

    // 現状は「1 oct cell = 1 ray = 1 result」前提なので atlas への直書きで衝突しない。
    // 将来 multi-ray per oct cell へ変更する場合は、ここを加算/集約方式へ差し替えること。
    if(oct_cell_index == 0u)
    {
        probe_pool_data.last_update_frame = cb_instant_rdv.frame_count;
        RWVspProbePoolBuffer[probe_index] = probe_pool_data;
    }
}

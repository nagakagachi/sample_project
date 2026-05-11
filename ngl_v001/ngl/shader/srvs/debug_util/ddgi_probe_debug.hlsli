/*
    ddgi_probe_debug.hlsli
    Dense DDGI probe debug draw.
*/

#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

struct VS_INPUT
{
    uint vertex_id : SV_VertexID;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 probe_pos_ws : POSITION_WS;
    uint global_cell_index : CELLINDEX0;
    uint cascade_index : CASCADEINDEX0;
};

VS_OUTPUT main_vs(VS_INPUT input)
{
    const float3 particle_quad_pos[4] = {
        float3(-1.0, -1.0, 0.0),
        float3(-1.0,  1.0, 0.0),
        float3( 1.0, -1.0, 0.0),
        float3( 1.0,  1.0, 0.0),
    };
    const float2 particle_quad_uv[4] = {
        float2(0.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 1.0),
        float2(1.0, 0.0),
    };
    const uint particle_quad_index[6] = { 0, 1, 2, 2, 1, 3 };

    VS_OUTPUT output = (VS_OUTPUT)0;
    const uint instance_id = input.vertex_id / 6;
    const uint instance_vtx_id = input.vertex_id % 6;

    uint cascade_index = 0;
    uint local_cell_index = 0;
    if(!DdgiDecodeGlobalCellIndex(instance_id, cascade_index, local_cell_index))
    {
        output.pos = 0.0.xxxx;
        return output;
    }

    const bool is_selected_cascade = (cb_srvs.debug_ddgi_probe_cascade < 0) || (cb_srvs.debug_ddgi_probe_cascade == int(cascade_index));
    const float draw_scale = is_selected_cascade ? cb_srvs.debug_probe_radius : 0.0;
    const float3 instance_pos = DdgiCalcCellCenterWs(cascade_index, local_cell_index);

    const int vtx_index = particle_quad_index[instance_vtx_id];
    float3 quad_vtx_pos = particle_quad_pos[vtx_index] * draw_scale;
    quad_vtx_pos = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(quad_vtx_pos, 0.0)).xyz;

    const float3 pos_ws = quad_vtx_pos + instance_pos;
    const float3 pos_vs = mul(cb_ngl_sceneview.cb_view_mtx, float4(pos_ws, 1.0)).xyz;
    output.pos = mul(cb_ngl_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));
    output.uv = particle_quad_uv[vtx_index];
    output.probe_pos_ws = instance_pos;
    output.global_cell_index = instance_id;
    output.cascade_index = cascade_index;
    return output;
}

float4 main_ps(VS_OUTPUT input) : SV_TARGET0
{
    const float2 unit_dist = (input.uv - float2(0.5, 0.5)) * float2(2.0, -2.0);
    const float unit_dist_len_sq = dot(unit_dist, unit_dist);
    if(1.0 < unit_dist_len_sq)
    {
        discard;
    }

    const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 dir_to_camera = normalize(view_origin - input.probe_pos_ws);
    const float3 camera_up = GetViewUpDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 quad_pose_side = normalize(cross(camera_up, -dir_to_camera));
    const float3 quad_pose_up = normalize(cross(-dir_to_camera, quad_pose_side));
    float3 normal_ws = float3(unit_dist.x, unit_dist.y, sqrt(saturate(1.0 - unit_dist_len_sq)));
    normal_ws = normalize(normal_ws.x * quad_pose_side + normal_ws.y * quad_pose_up + normal_ws.z * dir_to_camera);

    const uint sh_base = input.global_cell_index * 4;
    const float4 coeff0 = DdgiProbePackedShBuffer[sh_base + 0];
    const float4 coeff1 = DdgiProbePackedShBuffer[sh_base + 1];
    const float4 coeff2 = DdgiProbePackedShBuffer[sh_base + 2];
    const float4 coeff3 = DdgiProbePackedShBuffer[sh_base + 3];
    const float4 sh_basis = EvaluateL1ShBasis(normal_ws);
    const float4 sh_sky = float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a);
    const float4 sh_rad_r = float4(coeff0.r, coeff1.r, coeff2.r, coeff3.r);
    const float4 sh_rad_g = float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g);
    const float4 sh_rad_b = float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b);

    if(0 == cb_srvs.debug_ddgi_probe_mode)
    {
        const float hashed = frac(float(input.cascade_index) * 0.38196601125);
        return float4(hashed, frac(hashed * 1.71), frac(hashed * 2.37), 1.0);
    }
    if(1 == cb_srvs.debug_ddgi_probe_mode)
    {
        const float3 sh_radiance = max(0.0.xxx, float3(dot(sh_rad_r, sh_basis), dot(sh_rad_g, sh_basis), dot(sh_rad_b, sh_basis)));
        const float3 mapped = sh_radiance / (1.0 + sh_radiance);
        return float4(pow(mapped, 1.0 / 2.2), 1.0);
    }
    if(2 == cb_srvs.debug_ddgi_probe_mode)
    {
        return max(0.0, dot(sh_sky, sh_basis)).xxxx;
    }

    const uint dist_base = input.global_cell_index * 8;
    const float4 mean_coeff = float4(
        DdgiProbeDistanceMomentBuffer[dist_base + 0].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 1].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 2].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 3].x);
    const float mean_distance = max(0.0, dot(mean_coeff, sh_basis));
    if(3 == cb_srvs.debug_ddgi_probe_mode)
    {
        return mean_distance.xxxx;
    }

    const float4 mean2_coeff = float4(
        DdgiProbeDistanceMomentBuffer[dist_base + 4].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 5].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 6].x,
        DdgiProbeDistanceMomentBuffer[dist_base + 7].x);
    const float mean2_distance = max(0.0, dot(mean2_coeff, sh_basis));
    const float variance = max(mean2_distance - mean_distance * mean_distance, 0.0);
    if(4 == cb_srvs.debug_ddgi_probe_mode)
    {
        const float variance_vis = variance / (0.1 + variance);
        return float4(lerp(float3(0.02, 0.02, 0.05), float3(1.0, 0.35, 0.1), variance_vis), 1.0);
    }

    const float3 sample_to_probe_dir = normalize(input.probe_pos_ws - view_origin);
    const float4 sample_basis = EvaluateL1ShBasis(sample_to_probe_dir);
    const float mean_sample = max(0.0, dot(mean_coeff, sample_basis));
    const float mean2_sample = max(0.0, dot(mean2_coeff, sample_basis));
    const float variance_sample = max(mean2_sample - mean_sample * mean_sample, cb_srvs.ddgi_visibility_variance_bias);
    const float distance_to_probe = length(input.probe_pos_ws - view_origin);
    const float delta = max(distance_to_probe - mean_sample, 0.0);
    const float p_max = variance_sample / (variance_sample + delta * delta);
    const float visibility = max(pow(saturate(p_max), max(cb_srvs.ddgi_visibility_sharpness, 1e-3)), cb_srvs.ddgi_visibility_min_weight);
    if(5 == cb_srvs.debug_ddgi_probe_mode)
    {
        return visibility.xxxx;
    }
    if(6 == cb_srvs.debug_ddgi_probe_mode)
    {
        return (delta / (1.0 + delta)).xxxx;
    }
    if(7 == cb_srvs.debug_ddgi_probe_mode)
    {
        return saturate(p_max).xxxx;
    }
    return 0.0.xxxx;
}

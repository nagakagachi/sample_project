/*
    probe_debug.hlsli

    Probeデバッグ描画.
*/


#include "../srvs_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

struct VS_INPUT
{
	uint vertex_id	:	SV_VertexID;
};

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
    float2 uv  :   TEXCOORD0;
    float4 color : COLOR0;
    float3 pos_ws : POSITION_WS;
    float3 voxel_probe_pos_ws : VOXELPROBEPOSWS0;
    int voxel_index : VOXELINDEX0;
    uint cascade_index : CASCADEINDEX0;
    uint probe_index : PROBEINDEX0;
    uint probe_flags : PROBEFLAGS0;
};



VS_OUTPUT main_vs(VS_INPUT input)
{
    // ビルボードクアッドジオメトリ.
    const float3 particle_quad_pos[4] = {
        float3(-1.0, -1.0, 0.0),
        float3( -1.0, 1.0, 0.0),
        float3(1.0,  -1.0, 0.0),
        float3( 1.0,  1.0, 0.0),
    };
    const float2 particle_quad_uv[4] = {
        float2(0.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 1.0),
        float2(1.0, 0.0),
    };
    const uint particle_quad_index[6] = {
        0, 1, 2,
        2, 1, 3,
    };


	VS_OUTPUT output = (VS_OUTPUT)0;

	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 camera_up = GetViewUpDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 camera_right = GetViewRightDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);


    //　VertexIDからインスタンスID,三角形ID,三角形内頂点IDを計算.
    const uint instance_id = input.vertex_id / 6;
    const uint instance_vtx_id = input.vertex_id % 6;


    const uint global_cell_index = instance_id;
    uint cascade_index = 0;
    uint local_cell_index = 0;
    if(!FspDecodeGlobalCellIndex(global_cell_index, cascade_index, local_cell_index))
    {
        output.pos = float4(0.0, 0.0, 0.0, 0.0);
        output.uv = 0.0.xx;
        output.color = 0.0.xxxx;
        output.pos_ws = 0.0.xxx;
        output.voxel_probe_pos_ws = 0.0.xxx;
        output.voxel_index = -1;
        output.cascade_index = 0;
        output.probe_index = k_fsp_invalid_probe_index;
        output.probe_flags = 0;
        return output;
    }

    const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
    const uint probe_index = FspCellProbeIndexBuffer[global_cell_index];
    const bool is_allocated = (probe_index != k_fsp_invalid_probe_index);
    FspProbePoolData probe_pool_data = (FspProbePoolData)0;
    if(is_allocated)
    {
        probe_pool_data = FspProbePoolBuffer[probe_index];
    }
    const float3 probe_offset = is_allocated ? decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * (cascade.grid.cell_size * 0.5) : float3(0.0, 0.0, 0.0);

    const float3 probe_pos_ws = FspCalcCellCenterWs(cascade_index, local_cell_index) + probe_offset;


    float4 color = float4(1,1,1,1);

    // 表示位置.
    const float3 instance_pos = probe_pos_ws;
    const bool is_selected_cascade = (cb_srvs.debug_fsp_probe_cascade < 0) || (cb_srvs.debug_fsp_probe_cascade == int(cascade_index));
    float draw_scale = (is_allocated && is_selected_cascade) ? cb_srvs.debug_probe_radius : 0.0;

    const int vtx_index = particle_quad_index[ instance_vtx_id ];
    float3 quad_vtx_pos = particle_quad_pos[vtx_index] * draw_scale;
    // ビルボード
    quad_vtx_pos = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(quad_vtx_pos, 0.0)).xyz;

    float3 pos_ws = quad_vtx_pos + instance_pos;
    float3 pos_vs = mul(cb_ngl_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
    float4 pos_cs = mul(cb_ngl_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));

    output.pos = pos_cs;
    output.uv = particle_quad_uv[vtx_index];
    output.color = color;
    output.pos_ws = pos_ws;

    output.voxel_probe_pos_ws = instance_pos;
    output.voxel_index = int(global_cell_index);
    output.cascade_index = cascade_index;
    output.probe_index = probe_index;
    output.probe_flags = probe_pool_data.flags;

	return output;
}


float4 main_ps(VS_OUTPUT input) : SV_TARGET0
{
    const float3 camera_up = GetViewUpDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    const float2 unit_dist = (input.uv - float2(0.5,0.5)) * float2(2.0, -2.0);
    const float unit_dist_len_sq = dot(unit_dist, unit_dist);
    if(1.0 < unit_dist_len_sq)
    {
        discard;
    }
    if(input.probe_index == k_fsp_invalid_probe_index)
    {
        discard;
    }
    const FspProbePoolData probe_pool_data = FspProbePoolBuffer[input.probe_index];

    const float3 dir_to_camera = normalize(view_origin - input.voxel_probe_pos_ws);
    const float3 quad_pose_side = normalize(cross(camera_up, -dir_to_camera));
    const float3 quad_pose_up = normalize(cross(-dir_to_camera, quad_pose_side));

    // 球面法線計算.
    float3 normal_ws = float3(unit_dist.x, unit_dist.y, sqrt(saturate(1.0 - unit_dist_len_sq)));
    normal_ws = (normal_ws.x * quad_pose_side + normal_ws.y * quad_pose_up + normal_ws.z * dir_to_camera);
    normal_ws = normalize(normal_ws);


    const uint2 oct_cell_id = min(uint2(OctEncode(normal_ws) * k_fsp_probe_octmap_width), uint2(k_fsp_probe_octmap_width - 1, k_fsp_probe_octmap_width - 1));
    const uint2 octmap_texel_pos = FspProbeAtlasTexelCoord(input.probe_index, oct_cell_id);
    const float4 octmap_sample = FspProbeAtlasTex.Load(int3(octmap_texel_pos, 0));
    const int2 probe_tile_id = int2(int(input.probe_index % uint(cb_srvs.fsp_probe_atlas_tile_width)), int(input.probe_index / uint(cb_srvs.fsp_probe_atlas_tile_width)));
    const float4 sh_basis = EvaluateL1ShBasis(normal_ws);
    const float4 sh_sky_vis = float4(
        FspPackedShAtlasLoadCoeff(probe_tile_id, 0).r,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 1).r,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 2).r,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 3).r);
    const float4 sh_radiance_r = float4(
        FspPackedShAtlasLoadCoeff(probe_tile_id, 0).g,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 1).g,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 2).g,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 3).g);
    const float4 sh_radiance_g = float4(
        FspPackedShAtlasLoadCoeff(probe_tile_id, 0).b,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 1).b,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 2).b,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 3).b);
    const float4 sh_radiance_b = float4(
        FspPackedShAtlasLoadCoeff(probe_tile_id, 0).a,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 1).a,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 2).a,
        FspPackedShAtlasLoadCoeff(probe_tile_id, 3).a);


    
    float4 color = float4(normal_ws * 0.5 + 0.5, 1.0);// デフォルトでは法線を仮表示.

    // 可視化.
    if(0 == cb_srvs.debug_fsp_probe_mode)
    {
        const bool observed_this_frame = (probe_pool_data.last_seen_frame == cb_srvs.frame_count);
        color = observed_this_frame ? float4(0.2, 1.0, 0.3, 1.0) : float4(1.0, 0.85, 0.2, 1.0);
    }
    else if(1 == cb_srvs.debug_fsp_probe_mode)
    {
        color = probe_pool_data.avg_sky_visibility.xxxx;
    }
    else if(2 == cb_srvs.debug_fsp_probe_mode)
    {
        const float hashed = frac(float(input.probe_index) * 0.61803398875);
        color = float4(hashed, frac(hashed * 1.37), frac(hashed * 2.11), 1.0);
    }
    else if(3 == cb_srvs.debug_fsp_probe_mode)
    {
        const float age = float(cb_srvs.frame_count - probe_pool_data.last_seen_frame);
        const float age_norm = saturate(age / 30.0);
        color = lerp(float4(0.2, 1.0, 0.3, 1.0), float4(1.0, 0.2, 0.1, 1.0), age_norm);
    }
    else if(4 == cb_srvs.debug_fsp_probe_mode)
    {
        const float hashed = frac(float(input.cascade_index) * 0.38196601125);
        color = float4(hashed, frac(hashed * 1.71), frac(hashed * 2.37), 1.0);
    }
    else if(5 == cb_srvs.debug_fsp_probe_mode)
    {
        const float3 radiance = octmap_sample.rgb / (1.0 + octmap_sample.rgb);
        color = float4(pow(max(radiance, 0.0.xxx), 1.0 / 2.2), 1.0);
    }
    else if(6 == cb_srvs.debug_fsp_probe_mode)
    {
        color = octmap_sample.aaaa;
    }
    else if(7 == cb_srvs.debug_fsp_probe_mode)
    {
        const float3 sh_radiance = max(0.0.xxx, float3(
            dot(sh_radiance_r, sh_basis),
            dot(sh_radiance_g, sh_basis),
            dot(sh_radiance_b, sh_basis)));
        const float3 mapped_radiance = sh_radiance / (1.0 + sh_radiance);
        color = float4(pow(mapped_radiance, 1.0 / 2.2), 1.0);
    }
    else if(8 == cb_srvs.debug_fsp_probe_mode)
    {
        const float sh_sky_visibility = max(0.0, dot(sh_sky_vis, sh_basis));
        color = sh_sky_visibility.xxxx;
    }

	return color;
}

/*
    probe_debug.hlsli

    Probeデバッグ描画.
*/


#include "../instant_rdv_util.hlsli"
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
    float3 relocated_probe_pos_ws : RELOCATEDPROBEPOSWS0;
    uint cascade_index : CASCADEINDEX0;
    uint global_cell_index : GLOBALCELLINDEX0;
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
    if(!VspDecodeGlobalCellIndex(global_cell_index, cascade_index, local_cell_index))
    {
        output.pos = float4(0.0, 0.0, 0.0, 0.0);
        output.uv = 0.0.xx;
        output.color = 0.0.xxxx;
        output.pos_ws = 0.0.xxx;
        output.voxel_probe_pos_ws = 0.0.xxx;
        output.relocated_probe_pos_ws = 0.0.xxx;
        output.cascade_index = 0;
        output.global_cell_index = k_vsp_invalid_probe_index;
        output.probe_index = k_vsp_invalid_probe_index;
        output.probe_flags = 0;
        return output;
    }

    const VspCascadeGridParam cascade = VspGetCascadeParam(cascade_index);
    uint probe_index = k_vsp_invalid_probe_index;
    VspProbePoolData probe_pool_data = (VspProbePoolData)0;
    const bool is_allocated = VspTryGetActiveProbeForCell(probe_index, probe_pool_data, global_cell_index);
    const bool use_relocated_probe_pos = (0 != cb_instant_rdv.debug_vsp_probe_use_relocated_pos);
    const float3 probe_offset = (is_allocated && use_relocated_probe_pos) ? decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * (cascade.grid.cell_size * cb_instant_rdv.vsp_relocation_offset_scale_for_cascade_cell_size) : float3(0.0, 0.0, 0.0);

    const bool is_irradiance_volume_debug = (0 <= cb_instant_rdv.debug_vsp_irradiance_volume_mode);
    const float3 cell_center_ws = VspCalcCellCenterWs(cascade_index, local_cell_index);
    // IrradianceVolume debugはActiveProbe配置ではなく、dense cell中心に有効SHセルを表示する。
    const float3 probe_pos_ws = is_irradiance_volume_debug ? cell_center_ws : (cell_center_ws + probe_offset);


    float4 color = float4(1,1,1,1);

    // 表示位置.
    const float3 instance_pos = probe_pos_ws;
    const bool is_selected_cascade = (cb_instant_rdv.debug_vsp_probe_cascade < 0) || (cb_instant_rdv.debug_vsp_probe_cascade == int(cascade_index));
    bool is_drawable = is_selected_cascade && is_allocated;
    if(is_irradiance_volume_debug)
    {
        // IrradianceVolume表示だけが有効SH判定を必要とする。通常のProbe表示での4回のTexture loadを避ける。
        is_drawable = is_selected_cascade && VspIrradianceVolumeHasValidSignals(global_cell_index);
    }
    const float draw_scale = is_drawable ? cb_instant_rdv.debug_probe_radius : 0.0;

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
    output.relocated_probe_pos_ws = cell_center_ws + ((is_allocated) ? decode_uint_to_range1_vec3(probe_pool_data.probe_offset_v3) * (cascade.grid.cell_size * cb_instant_rdv.vsp_relocation_offset_scale_for_cascade_cell_size) : 0.0.xxx);
    output.cascade_index = cascade_index;
    output.global_cell_index = global_cell_index;
    output.probe_index = probe_index;
    output.probe_flags = is_allocated ? 1u : 0u;

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
    const bool is_irradiance_volume_debug = (0 <= cb_instant_rdv.debug_vsp_irradiance_volume_mode);
    if((!is_irradiance_volume_debug) && input.probe_index == k_vsp_invalid_probe_index)
    {
        discard;
    }
    VspProbePoolData probe_pool_data = (VspProbePoolData)0;
    if(!is_irradiance_volume_debug)
    {
        probe_pool_data = VspProbePoolBuffer[input.probe_index];
        if(probe_pool_data.owner_cell_index == k_vsp_invalid_probe_index)
        {
            discard;
        }
    }

    const float3 dir_to_camera = normalize(view_origin - input.voxel_probe_pos_ws);
    const float3 quad_pose_side = normalize(cross(camera_up, -dir_to_camera));
    const float3 quad_pose_up = normalize(cross(-dir_to_camera, quad_pose_side));

    // 球面法線計算.
    float3 normal_ws = float3(unit_dist.x, unit_dist.y, sqrt(saturate(1.0 - unit_dist_len_sq)));
    normal_ws = (normal_ws.x * quad_pose_side + normal_ws.y * quad_pose_up + normal_ws.z * dir_to_camera);
    normal_ws = normalize(normal_ws);


    float4 octmap_sample = 0.0.xxxx;
    if(!is_irradiance_volume_debug)
    {
        const uint2 oct_cell_id = min(uint2(OctEncode(normal_ws) * k_vsp_probe_octmap_width), uint2(k_vsp_probe_octmap_width - 1, k_vsp_probe_octmap_width - 1));
        const uint2 octmap_texel_pos = VspProbeAtlasTexelCoord(input.probe_index, oct_cell_id);
        octmap_sample = VspProbeAtlasTex.Load(int3(octmap_texel_pos, 0));
    }
    const uint global_cell_index = input.global_cell_index;
    const int debug_mode = cb_instant_rdv.debug_vsp_probe_mode;
    const int irradiance_volume_mode = cb_instant_rdv.debug_vsp_irradiance_volume_mode;
    const bool needs_volume_sh =
        is_irradiance_volume_debug || debug_mode == 6 || debug_mode == 7;
    const bool needs_radiance_sh = debug_mode == 6 || irradiance_volume_mode == 0;
    float4 sh_sky_vis = 0.0.xxxx;
    float4 sh_radiance_r = 0.0.xxxx;
    float4 sh_radiance_g = 0.0.xxxx;
    float4 sh_radiance_b = 0.0.xxxx;
    if(needs_volume_sh)
    {
        float4 sh_irradiance_r;
        float4 sh_irradiance_g;
        float4 sh_irradiance_b;
        VspIrradianceVolumeLoadSignals(
            global_cell_index,
            sh_sky_vis,
            sh_irradiance_r,
            sh_irradiance_g,
            sh_irradiance_b);

        if(needs_radiance_sh)
        {
            // TextureにはCos畳み込み済みDiffuse Irradianceを格納するため、Radiance表示だけ逆畳み込みする。
            const float pi = 3.14159265359;
            const float4 deconvolve_scale = float4(1.0 / pi, 3.0 / (2.0 * pi), 3.0 / (2.0 * pi), 3.0 / (2.0 * pi));
            sh_radiance_r = sh_irradiance_r * deconvolve_scale;
            sh_radiance_g = sh_irradiance_g * deconvolve_scale;
            sh_radiance_b = sh_irradiance_b * deconvolve_scale;
        }
    }
    const float4 sh_basis = EvaluateL1ShBasis(normal_ws);


    
    float4 color = float4(normal_ws * 0.5 + 0.5, 1.0);// デフォルトでは法線を仮表示.

    // 可視化.
    if(0 == debug_mode)
    {
        const bool observed_this_frame = (probe_pool_data.last_seen_frame == cb_instant_rdv.frame_count);
        color = observed_this_frame ? float4(0.2, 1.0, 0.3, 1.0) : float4(1.0, 0.85, 0.2, 1.0);
    }
    else if(1 == debug_mode)
    {
        const float hashed = frac(float(input.probe_index) * 0.61803398875);
        color = float4(hashed, frac(hashed * 1.37), frac(hashed * 2.11), 1.0);
    }
    else if(2 == debug_mode)
    {
        const float age = float(cb_instant_rdv.frame_count - probe_pool_data.last_seen_frame);
        const float age_norm = saturate(age / 30.0);
        color = lerp(float4(0.2, 1.0, 0.3, 1.0), float4(1.0, 0.2, 0.1, 1.0), age_norm);
    }
    else if(3 == debug_mode)
    {
        const float hashed = frac(float(input.cascade_index) * 0.38196601125);
        color = float4(hashed, frac(hashed * 1.71), frac(hashed * 2.37), 1.0);
    }
    else if(4 == debug_mode)
    {
        const float3 radiance = octmap_sample.rgb / (1.0 + octmap_sample.rgb);
        color = float4(pow(max(radiance, 0.0.xxx), 1.0 / 2.2), 1.0);
    }
    else if(5 == debug_mode)
    {
        color = octmap_sample.aaaa;
    }
    else if(6 == debug_mode)
    {
        const float3 sh_radiance = max(0.0.xxx, float3(
            dot(sh_radiance_r, sh_basis),
            dot(sh_radiance_g, sh_basis),
            dot(sh_radiance_b, sh_basis)));
        const float3 mapped_radiance = sh_radiance / (1.0 + sh_radiance);
        color = float4(pow(mapped_radiance, 1.0 / 2.2), 1.0);
    }
    else if(7 == debug_mode)
    {
        const float sh_sky_visibility = max(0.0, dot(sh_sky_vis, sh_basis));
        color = sh_sky_visibility.xxxx;
    }
    else if(8 == debug_mode)
    {
        const float3 bbv_voxel_coord_f =
            (input.voxel_probe_pos_ws - cb_instant_rdv.bbv.grid_min_pos) * cb_instant_rdv.bbv.cell_size_inv;
        const int3 bbv_voxel_coord = int3(floor(bbv_voxel_coord_f));
        const bool inside_bbv = all(bbv_voxel_coord >= 0) &&
            all(bbv_voxel_coord < cb_instant_rdv.bbv.grid_resolution);
        const bool embedded_in_bbv = inside_bbv &&
            (0u != read_bbv_voxel_from_world_pos(
                BitmaskBrickVoxel,
                cb_instant_rdv.bbv.grid_resolution,
                cb_instant_rdv.bbv.grid_toroidal_offset,
                cb_instant_rdv.bbv.grid_min_pos,
                cb_instant_rdv.bbv.cell_size_inv,
                input.voxel_probe_pos_ws));
        color = !inside_bbv
            ? float4(0.2, 0.5, 1.0, 1.0)
            : (embedded_in_bbv ? float4(1.0, 0.15, 0.1, 1.0) : float4(0.2, 1.0, 0.3, 1.0));
    }
    else if(9 == debug_mode)
    {
        const float3 camera_position_ws = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
        const float3 segment = input.relocated_probe_pos_ws - camera_position_ws;
        const float segment_length = length(segment);
        const float3 segment_direction = (segment_length > 1e-4) ? segment / segment_length : float3(0.0, 0.0, 1.0);
        const float3 grid_min_ws = cb_instant_rdv.bbv.grid_min_pos;
        const float3 grid_max_ws = grid_min_ws + float3(cb_instant_rdv.bbv.grid_resolution) * cb_instant_rdv.bbv.cell_size;
        const bool target_inside_bbv = all(input.relocated_probe_pos_ws >= grid_min_ws) && all(input.relocated_probe_pos_ws < grid_max_ws);
        int hit_voxel_index = -1;
        float4 trace_debug = 0.0.xxxx;
        const float4 hit = trace_bbv(
            hit_voxel_index,
            trace_debug,
            camera_position_ws,
            segment_direction,
            segment_length,
            cb_instant_rdv.bbv.grid_min_pos,
            cb_instant_rdv.bbv.cell_size,
            cb_instant_rdv.bbv.grid_resolution,
            cb_instant_rdv.bbv.grid_toroidal_offset,
            BitmaskBrickVoxel);
        const bool blocked = target_inside_bbv && hit.x >= 0.0;
        color = !target_inside_bbv
            ? float4(0.2, 0.5, 1.0, 1.0)
            : (blocked ? float4(1.0, 0.15, 0.1, 1.0) : float4(0.2, 1.0, 0.3, 1.0));
    }
    else if(0 == irradiance_volume_mode)
    {
        const float3 sh_radiance = max(0.0.xxx, float3(
            dot(sh_radiance_r, sh_basis),
            dot(sh_radiance_g, sh_basis),
            dot(sh_radiance_b, sh_basis)));
        const float3 mapped_radiance = sh_radiance / (1.0 + sh_radiance);
        color = float4(pow(mapped_radiance, 1.0 / 2.2), 1.0);
    }
    else if(1 == irradiance_volume_mode)
    {
        const float sh_sky_visibility = max(0.0, dot(sh_sky_vis, sh_basis));
        color = sh_sky_visibility.xxxx;
    }
	return color;
}

/*
    probe_debug.hlsli

    Probeデバッグ描画.
*/


#include "../ssvg_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

SamplerState        SmpLinearClamp;

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

	const float3 camera_dir = GetViewDirFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);
    const float3 camera_up = GetViewUpDirFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);
    const float3 camera_right = GetViewRightDirFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);
	const float3 camera_pos = GetViewPosFromInverseViewMatrix(ngl_cb_sceneview.cb_view_inv_mtx);


    //　VertexIDからインスタンスID,三角形ID,三角形内頂点IDを計算.
    const uint instance_id = input.vertex_id / 6;
    const uint instance_vtx_id = input.vertex_id % 6;


    const int3 voxel_coord = index_to_voxel_coord(instance_id, cb_ssvg.wcp.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.wcp.grid_toroidal_offset, cb_ssvg.wcp.grid_resolution), cb_ssvg.wcp.grid_resolution);

    #if 1
        const float3 probe_offset = decode_uint_to_range1_vec3(WcpProbeBuffer[voxel_index].probe_offset_v3) * (cb_ssvg.wcp.cell_size * 0.5);
    #else
        const float3 probe_offset = float3(0.0, 0.0, 0.0);
    #endif

    const float3 probe_pos_ws = (float3(voxel_coord) + (0.5)) * cb_ssvg.wcp.cell_size + cb_ssvg.wcp.grid_min_pos + probe_offset;


    float4 color = float4(1,1,1,1);

    // 表示位置.
    const float3 instance_pos = probe_pos_ws;
    float draw_scale = cb_ssvg.debug_probe_radius;

    const int vtx_index = particle_quad_index[ instance_vtx_id ];
    float3 quad_vtx_pos = particle_quad_pos[vtx_index] * draw_scale;
    // ビルボード
    quad_vtx_pos = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(quad_vtx_pos, 0.0)).xyz;

    float3 pos_ws = quad_vtx_pos + instance_pos;
    float3 pos_vs = mul(ngl_cb_sceneview.cb_view_mtx, float4(pos_ws, 1.0));
    float4 pos_cs = mul(ngl_cb_sceneview.cb_proj_mtx, float4(pos_vs, 1.0));

    output.pos = pos_cs;
    output.uv = particle_quad_uv[vtx_index];
    output.color = color;
    output.pos_ws = pos_ws;

    output.voxel_probe_pos_ws = instance_pos;
    output.voxel_index = int(voxel_index);

	return output;
}


float4 main_ps(VS_OUTPUT input) : SV_TARGET0
{
    const float3 camera_up = ngl_cb_sceneview.cb_view_inv_mtx._m01_m11_m21;
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

    const float2 unit_dist = (input.uv - float2(0.5,0.5)) * float2(2.0, -2.0);
    const float unit_dist_len_sq = dot(unit_dist, unit_dist);
    if(1.0 < unit_dist_len_sq)
    {
        discard;
    }
    const int voxel_index = input.voxel_index;

    const float3 dir_to_camera = normalize(camera_pos - input.voxel_probe_pos_ws);
    const float3 quad_pose_side = normalize(cross(camera_up, -dir_to_camera));
    const float3 quad_pose_up = normalize(cross(-dir_to_camera, quad_pose_side));

    // 球面法線計算.
    float3 normal_ws = float3(unit_dist.x, unit_dist.y, sqrt(saturate(1.0 - unit_dist_len_sq)));
    normal_ws = (normal_ws.x * quad_pose_side + normal_ws.y * quad_pose_up + normal_ws.z * dir_to_camera);
    normal_ws = normalize(normal_ws);


    const uint2 probe_2d_map_pos = uint2(voxel_index % cb_ssvg.wcp.flatten_2d_width, voxel_index / cb_ssvg.wcp.flatten_2d_width);
    uint tex_width, tex_height;
    WcpProbeAtlasTex.GetDimensions(tex_width, tex_height);
    const float2 octmap_texel_pos = float2(probe_2d_map_pos * k_probe_octmap_width_with_border + 1.0) + OctEncode(normal_ws)*k_probe_octmap_width;


    
    float4 color = float4(normal_ws * 0.5 + 0.5, 1.0);// デフォルトでは法線を仮表示.

    // 可視化.
    if(0 == cb_ssvg.debug_wcp_probe_mode)
    {
        const WcpProbeData probe_data = WcpProbeBuffer[voxel_index];
        color = probe_data.avg_sky_visibility.xxxx;
    }
    else if(1 == cb_ssvg.debug_wcp_probe_mode)
    {
        // WcpProbeAtlasTex に格納されたOctmapを可視化.
        const float4 probe_data = WcpProbeAtlasTex.Load(uint3(octmap_texel_pos, 0));

        color = pow(probe_data.xxxx, 2.0);// 適当ガンマ
    }
    else if(2 == cb_ssvg.debug_wcp_probe_mode)
    {
        // WcpProbeAtlasTex に格納されたOctmapを可視化.
        // Samplerで補間取得
        const float4 probe_data = WcpProbeAtlasTex.SampleLevel(SmpLinearClamp, (octmap_texel_pos) / float2(tex_width, tex_height), 0);

        color = pow(probe_data.xxxx, 2.0);// 適当ガンマ
    }

	return color;
}
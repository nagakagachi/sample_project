/*
    voxel_probe_debug_vs.hlsl

    Voxel Probeデバッグ描画.
*/


#include "../ssvg_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

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

	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 camera_up = GetViewUpDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    const float3 camera_right = GetViewRightDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 camera_pos = GetViewPosFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    //　VertexIDからインスタンスID,三角形ID,三角形内頂点IDを計算.
    const uint instance_id = input.vertex_id / 6;
    const uint instance_vtx_id = input.vertex_id % 6;


    const int3 voxel_coord = index_to_voxel_coord(instance_id, cb_ssvg.bbv.grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_coord, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_resolution), cb_ssvg.bbv.grid_resolution);

    // Bbv固有データ.
    const uint unique_data_addr = bbv_voxel_unique_data_addr(voxel_index);
    const uint bbv_occupied_flag = BitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(voxel_index)] & k_bbv_per_voxel_bitmask_u32_component_mask;
    

    // Bbv追加データ.
    const BbvOptionalData voxel_optional_data = BitmaskBrickVoxelOptionData[voxel_index];
    const bool is_invalid_probe_local_pos = (0 == voxel_optional_data.probe_pos_code);
    const int3 probe_coord_in_voxel = (is_invalid_probe_local_pos) ? int3(k_bbv_per_voxel_resolution.xxx * 0.5) : calc_bbv_bitcell_pos_from_bit_index(calc_bbv_probe_bitcell_index(voxel_optional_data));
    const float3 probe_pos_ws = (float3(voxel_coord) + (float3(probe_coord_in_voxel) + 0.5) / float(k_bbv_per_voxel_resolution)) * cb_ssvg.bbv.cell_size + cb_ssvg.bbv.grid_min_pos;


    float4 color = float4(1,1,1,1);

    // 表示位置.
    const float3 instance_pos = probe_pos_ws;
    float draw_scale = cb_ssvg.debug_probe_radius;
    if(0 == (bbv_occupied_flag))
    {
        // ジオメトリのないVoxelは小さく表示.
        draw_scale *= cb_ssvg.debug_probe_near_geom_scale;
    }
    if(is_invalid_probe_local_pos)
    {
        // 埋まり回避プローブ位置が無い場合は色変え.
        color = float4(0,0,1,1);
    }

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
    output.voxel_index = int(voxel_index);

	return output;
}


float4 main_ps(VS_OUTPUT input) : SV_TARGET0
{
    const float3 camera_up = cb_ngl_sceneview.cb_view_inv_mtx._m01_m11_m21;
	const float3 camera_pos = cb_ngl_sceneview.cb_view_inv_mtx._m03_m13_m23;

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

    
    float4 color = float4(normal_ws * 0.5 + 0.5, 1.0);// デフォルトでは法線を仮表示.

    const int normal_principal_axis = calc_principal_axis_component_index(abs(normal_ws));
    int component_index = 0;
    if(0 == normal_principal_axis)
    {
        component_index = (0 < normal_ws.x) ? 0 : 1;
    }
    else if(1 == normal_principal_axis)
    {
        component_index = (0 < normal_ws.y) ? 2 : 3;
    }
    else
    {
        component_index = (0 < normal_ws.z) ? 4 : 5;
    }

    // Bbv追加データ.
    const BbvOptionalData voxel_optional_data = BitmaskBrickVoxelOptionData[voxel_index];

    if(0 == cb_ssvg.debug_bbv_probe_mode)
    {
        const float surface_distance = length_int_vector3(voxel_optional_data.to_surface_vector);
        #if 1
            // 距離をグレースケールで可視化.
            float distance_color = saturate(surface_distance/8.0);
            
            distance_color = pow(distance_color, 2.0);// 適当ガンマ
            color = float4(distance_color, distance_color, distance_color, 1.0);
        #else
            // 整数距離を色変えでわかりやすく可視化.
            if(0.0 > surface_distance)
            {
                color = float4(0,0,0.25,1);
            }
            else if((1<<10)*3 == surface_distance)
            {
                color = float4(0,1,1,1);// 無効値.
            }
            else if(0.0 == surface_distance)
            {
                color = float4(0,0,0,1);
            }
            else if(1.0 == surface_distance)
            {
                color = float4(1,0,0,1);
            }
            else if(2.0 == surface_distance)
            {
                color = float4(0,1,0,1);
            }
            else if(3.0 == surface_distance)
            {
                color = float4(0,0,1,1);
            }
            else
            {
                color = float4(1,1,1,1);
            }
        #endif
    }

	return color;
}
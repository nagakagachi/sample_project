/*
    obm_voxel_debug_vs.hlsl

    Voxel Probeデバッグ描画.
*/


#include "../ssvg_util.hlsli"
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

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

	const float3 camera_dir = ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22;// InvShadowViewMtxから向きベクトルを取得.
    const float3 camera_up = ngl_cb_sceneview.cb_view_inv_mtx._m01_m11_m21;
    const float3 camera_right = ngl_cb_sceneview.cb_view_inv_mtx._m00_m10_m20;
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

    //　VertexIDからインスタンスID,三角形ID,三角形内頂点IDを計算.
    const uint instance_id = input.vertex_id / 6;
    const uint instance_vtx_id = input.vertex_id % 6;


    const int3 voxel_coord = index_to_voxel_coord(instance_id, cb_dispatch_param.base_grid_resolution);
    const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.grid_toroidal_offset, cb_dispatch_param.base_grid_resolution), cb_dispatch_param.base_grid_resolution);
    const uint voxel_unique_data_addr = obm_voxel_unique_data_addr(voxel_index);

    const uint obm_voxel_unique_data = OccupancyBitmaskVoxel[voxel_unique_data_addr];
    const bool is_obm_empty = (0 == obm_voxel_unique_data);
    /*
    if(is_obm_empty)
    {
        // ジオメトリが無い場合の非表示.
        output.pos = float4(1.0/0.0,0,0,0);//NaN export culling.
        return output;
    }
    */
    
    const CoarseVoxelData coarse_voxel_data = CoarseVoxelBuffer[voxel_index];
    const bool is_invalid_probe_local_pos = (0 == coarse_voxel_data.probe_pos_index);
    const int3 probe_coord_in_voxel = (is_invalid_probe_local_pos) ? int3(0,0,0) : calc_occupancy_bitmask_cell_position_in_voxel_from_bit_index(coarse_voxel_data.probe_pos_index-1);
    const float3 probe_pos_ws = (float3(voxel_coord) + (float3(probe_coord_in_voxel) + 0.5) / float(k_obm_per_voxel_resolution)) * cb_dispatch_param.cell_size + cb_dispatch_param.grid_min_pos;


    float4 color = float4(1,1,1,1);

    // 表示位置.
    const float3 instance_pos = probe_pos_ws;
    float draw_scale = cb_dispatch_param.cell_size * 0.75 / k_obm_per_voxel_resolution;
    if(is_obm_empty)
    {
        // ジオメトリのないVoxelは小さく表示.
        draw_scale *= 0.3;
    }
    if(is_invalid_probe_local_pos)
    {
        // 埋まり回避プローブ位置が無い場合は色変え.
        color = float4(0,0,1,1);
    }

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

    
        // GI情報を可視化.
        const CoarseVoxelData coarse_voxel_data = CoarseVoxelBuffer[voxel_index];
        if(0 == cb_dispatch_param.debug_probe_mode)
        {
            // 指向性
            const float voxel_gi_average = coarse_voxel_data.sky_visibility_dir_avg[component_index];
            color.xyz = float4(voxel_gi_average, voxel_gi_average, voxel_gi_average, 1);
        }
        else if(1 == cb_dispatch_param.debug_probe_mode)
        {
            // 全方向平均.
            const float voxel_gi_average = 
            (coarse_voxel_data.sky_visibility_dir_avg[0] + coarse_voxel_data.sky_visibility_dir_avg[1]
            + coarse_voxel_data.sky_visibility_dir_avg[2] + coarse_voxel_data.sky_visibility_dir_avg[3]
            + coarse_voxel_data.sky_visibility_dir_avg[4] + coarse_voxel_data.sky_visibility_dir_avg[5]) / 6.0;
            color.xyz = float4(voxel_gi_average, voxel_gi_average, voxel_gi_average, 1);
        }


    //float4 color = float4(saturate(dot(normal_ws, -float3(0.0, -1.0, 0.0)).xxx), 1.0);
    //const float4 rand_seed = float4(voxel_index, voxel_index+1, voxel_index*2, voxel_index*3);
    //float4 color = float4(noise_iqint32(rand_seed), noise_iqint32(rand_seed.yzwx), noise_iqint32(rand_seed.zxyw), 1.0);

	return color;
}
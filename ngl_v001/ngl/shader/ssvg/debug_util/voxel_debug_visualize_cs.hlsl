
#if 0

ss_voxel_debug_visualize_cs.hlsl

デバッグ可視化.

#endif


#include "../ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

RWTexture2D<float4>	RWTexWork;


// デバッグテクスチャに対してDispatch.
[numthreads(16, 16, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_ssvg.tex_hw_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);
    

    if(4 == cb_ssvg.debug_view_mode)
    {
        // Voxel上面図X-Ray表示.
        const int3 bv_full_reso = cb_ssvg.bbv.grid_resolution * k_bbv_per_voxel_resolution;
        const float visualize_scale = 0.5;
        float3 read_pos_world_base = (float3(dtid.x, 0.0, cb_ssvg.tex_hw_depth_size.y-1 - dtid.y) + 0.5) * visualize_scale * cb_ssvg.bbv.cell_size/k_bbv_per_voxel_resolution;
        read_pos_world_base += cb_ssvg.bbv.grid_min_pos;

        float write_data = 0.0;
        for(int yi = 0; yi < bv_full_reso.y; ++yi)
        {
            const float3 read_pos_world = read_pos_world_base + float3(0.0, yi, 0.0) * (cb_ssvg.bbv.cell_size/k_bbv_per_voxel_resolution);

            const uint bit_value = read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_ssvg.bbv.grid_resolution, cb_ssvg.bbv.grid_toroidal_offset, cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size_inv, read_pos_world);

            float occupancy = float(bit_value);
            occupancy /= (float)bv_full_reso.y;

            write_data += occupancy * 8.0;
        }

        RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0);
    }
    else if(5 == cb_ssvg.debug_view_mode)
    {
        // WCP ProbeAtlas.
        if(any(cb_ssvg.wcp.flatten_2d_width * k_probe_octmap_width_with_border < dtid.xy))
            return;
        
        const float4 probe_data = WcpProbeAtlasTex.Load(uint3(dtid.xy, 0));
        RWTexWork[dtid.xy] = probe_data.xxxx;
    }
    else
    {
        // ViewRayでレイキャスト可視化.
	    const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	    const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;
        
        const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        const float3 ray_dir_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));

        const float trace_distance = 10000.0;
          
        int hit_voxel_index = -1;
        // Trace最適化検証.
        float4 curr_ray_t_ws = trace_ray_vs_bitmask_brick_voxel_grid(
            hit_voxel_index,
            camera_pos, ray_dir_ws, trace_distance, 
            cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
            cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel);

        float4 debug_color = float4(0, 0, 1, 0);
        if(0.0 <= curr_ray_t_ws.x)
        {
            const float fog_rate0 = pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2);
            const float fog_rate1 = saturate((curr_ray_t_ws.x - 70.0)/500.0);

            const uint unique_data_addr = bbv_voxel_unique_data_addr(hit_voxel_index);
            // デバッグ用テクスチャにモード別描画.
            if(0 == cb_ssvg.debug_view_mode)
            {
                // bbvセル可視化
                const float3 bbv_cell_id = floor((camera_pos + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_ssvg.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                debug_color.xyz = float4(noise_iqint32(bbv_cell_id.xyzz), noise_iqint32(bbv_cell_id.xzyy), noise_iqint32(bbv_cell_id.xyzx), 1);

                // 簡易フォグ.
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
            else if(1 == cb_ssvg.debug_view_mode)
            {
                // VoxelIDを可視化.
                debug_color.xyz = float4(noise_iqint32(hit_voxel_index), noise_iqint32(hit_voxel_index*2), noise_iqint32(hit_voxel_index*3), 1);
                
                // 簡易フォグ.
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
            else if(2 == cb_ssvg.debug_view_mode)
            {
                // VoxelIDを可視化.
                debug_color.xyz = float4(frac(hit_voxel_index / 64.0), frac(hit_voxel_index / 256.0), frac(hit_voxel_index / 1024.0), 1);
                
                // 簡易フォグ.
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
            else if(3 == cb_ssvg.debug_view_mode)
            {
                // BitmaskBrickVoxelセルの深度を可視化.
                debug_color.xyz = float4(saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0), 1);
            }
            else
            {
                // BitmaskBrickVoxelセルのヒット法線可視化.
                const float3 bbv_cell_id = floor((camera_pos + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_ssvg.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                debug_color.xyz = abs(curr_ray_t_ws.yzw);
                
                // 簡易フォグ.
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
        }
        RWTexWork[dtid.xy] = debug_color;
    }
}

#if 0

ss_voxel_debug_visualize_cs.hlsl

デバッグ可視化.

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

Buffer<uint>		BufferWork;
Buffer<uint>		OccupancyBitmaskVoxel;

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
	const float2 screen_size_f = float2(cb_dispatch_param.TexHardwareDepthSize.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);
    
    #if 1
        // ViewRayでレイキャスト可視化.
	    const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	    const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;
        
        const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        const float3 ray_dir_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));

        const float trace_distance = 10000.0;
          
        int hit_voxel_index = -1;
        float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
            hit_voxel_index,
            camera_pos, ray_dir_ws, trace_distance, 
            cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSize, cb_dispatch_param.BaseResolution,
            cb_dispatch_param.GridToroidalOffset, OccupancyBitmaskVoxel);

        //float4 debug_color = (0.0 <= curr_ray_t_ws.x) ? float4(curr_ray_t_ws.xxx, 1)/20.0 : float4(0, 0, 1, 0);
        float4 debug_color = float4(0, 0, 1, 0);
        if(0.0 <= curr_ray_t_ws.x)
        {
            const uint unique_data_addr = voxel_unique_data_addr(hit_voxel_index);
            const uint occupancy_count = OccupancyBitmaskVoxel[unique_data_addr];
            const float occupancy_f = float(occupancy_count) / float(k_per_voxel_occupancy_bit_count);
            
            
            const uint voxel_gi_data = BufferWork[hit_voxel_index];
            const uint voxel_gi_sample_count = voxel_gi_data & 0xFFFF;
            const uint voxel_gi_accumulated = (voxel_gi_data >> 16) & 0xFFFF;
            const float voxel_gi_average = (0 < voxel_gi_sample_count) ? float(voxel_gi_accumulated) / float(voxel_gi_sample_count) : 0.0;

            // 占有度合いを可視化.
            //debug_color.xyz = float4(occupancy_f, occupancy_f, occupancy_f, 1);
            // CoarseVoxelIDを可視化.
            //debug_color.xyz = float4(noise_iqint32(curr_ray_t_ws.yzww), noise_iqint32(curr_ray_t_ws.zwyy), noise_iqint32(curr_ray_t_ws.wyzz), 1);
            // GIサンプル数を可視化.
            debug_color.xyz = float4(voxel_gi_average, voxel_gi_average, voxel_gi_average, 1);



            // 簡易フォグ.
            //debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), pow(saturate(curr_ray_t_ws.x/50.0), 1.0/1.2));
            //debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), saturate((curr_ray_t_ws.x/100.0)));
        }
        RWTexWork[dtid.xy] = debug_color;
    #else
        // 上面図X-Ray表示.
        const int3 bv_full_reso = cb_dispatch_param.BaseResolution * k_per_voxel_occupancy_reso;
        
        const float visualize_scale = 0.5;
        float3 read_pos_world_base = float3(dtid.x, 0.0, cb_dispatch_param.TexHardwareDepthSize.y-1 - dtid.y) * visualize_scale * cb_dispatch_param.CellSize/k_per_voxel_occupancy_reso;
        read_pos_world_base += cb_dispatch_param.GridMinPos;

        float write_data = 0.0;
        for(int yi = 0; yi < bv_full_reso.y; ++yi)
        {
            const float3 read_pos_world = read_pos_world_base + float3(0.0, yi, 0.0) * (cb_dispatch_param.CellSize/k_per_voxel_occupancy_reso);

            const uint bit_value = read_occupancy_bitmask_voxel_from_world_pos(OccupancyBitmaskVoxel, cb_dispatch_param.BaseResolution, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSizeInv, read_pos_world);

            float occupancy = float(bit_value);
            occupancy /= (float)bv_full_reso.y;

            write_data += occupancy * 8.0;
        }

        RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0);
    #endif
}
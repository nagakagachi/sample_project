
#if 0

ss_voxel_debug_visualize_cs.hlsl

デバッグ可視化.

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
ConstantBuffer<DispatchParam> cb_dispatch_param;

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
        // レイキャストで可視化.
        
	    const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	    const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;
        
        const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        const float3 ray_dir_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));

        const float trace_distance = 10000.0;
          
        float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
            camera_pos, ray_dir_ws, trace_distance, 
            cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSize, cb_dispatch_param.BaseResolution,
            cb_dispatch_param.GridToroidalOffset, OccupancyBitmaskVoxel);
        #if 0
            RWTexWork[dtid.xy] = (0.0 <= curr_ray_t_ws.x) ? float4(curr_ray_t_ws.xxx, 1)/100.0 : float4(0, 0, 1, 0);
        #else
            float3 hit_normal = curr_ray_t_ws.yzw;
            hit_normal = any(isnan(hit_normal)) ? float3(1,1,0) : hit_normal;
            RWTexWork[dtid.xy] = (0.0 <= curr_ray_t_ws.x) ? float4( lerp(abs(hit_normal), float3(1.0,1.0,1.0), saturate(curr_ray_t_ws.x/100.0)), 1) : float4(0, 0, 0.3, 0);
        #endif
        
    #else
        // 上面でボクセル可視化.
        uint2 read_voxel_xz = dtid.xy / 8;// 1ボクセルを何ピクセルとして画面に出すか.
        // ビットマスクボクセルの解像度分描画する.
        const int3 bv_full_reso = cb_dispatch_param.BaseResolution * k_per_voxel_occupancy_reso;
        if(all(read_voxel_xz < bv_full_reso.xz))
        {
            float write_data = 0.0;
            for(int yi = 0; yi < bv_full_reso.y; ++yi)
            {
                const int3 bitmask_coord = int3(read_voxel_xz.x, yi, (bv_full_reso.z - 1) - read_voxel_xz.y);
                
                // bitmaskが格納されているボクセルを読み出し.
                const int3 voxel_coord = bitmask_coord / k_per_voxel_occupancy_reso;
                int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
                uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

                const int3 voxel_inner_coord = bitmask_coord - voxel_coord*k_per_voxel_occupancy_reso;
                
                uint bitmask_u32_offset;
                uint bitmask_u32_bit_pos;
                calc_occupancy_bitmask_voxel_inner_bit_info(bitmask_u32_offset, bitmask_u32_bit_pos, voxel_inner_coord);

                // 該当する位置のビットを取り出し.
                const uint voxel_elem_bitmask = OccupancyBitmaskVoxel[voxel_addr * k_per_voxel_occupancy_u32_count + bitmask_u32_offset];
                const uint occupancy_bit = (voxel_elem_bitmask >> bitmask_u32_bit_pos) & 0x1;

                float occupancy = float(occupancy_bit);
                occupancy /= (float)bv_full_reso.y;

                write_data += occupancy * 8.0;
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0);
        }
    #endif
}

#if 0

coarse_voxel_update_cs.hlsl

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;

RWBuffer<uint>		RWOccupancyBitmaskVoxel;
Buffer<uint>		OccupancyBitmaskVoxel;
RWBuffer<uint>		RWBufferWork;

// DepthBufferに対してDispatch.
[numthreads(128, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

    const uint voxel_count = cb_dispatch_param.BaseResolution.x * cb_dispatch_param.BaseResolution.y * cb_dispatch_param.BaseResolution.z;
    
    #if 1
        // toroidalマッピング考慮
        const int3 voxel_coord = index_to_voxel_coord(dtid.x, cb_dispatch_param.BaseResolution);
        const int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
        const uint voxel_index = voxel_coord_to_index(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);
    #else
        const uint voxel_index = dtid.x;
    #endif

    if(voxel_index < voxel_count)
    {
        const uint unique_data_addr = voxel_unique_data_addr(voxel_index);
        const uint obm_addr = voxel_occupancy_bitmask_data_addr(voxel_index);

        uint obm_count = 0;
        for(uint i = 0; i < voxel_occupancy_bitmask_uint_count(); i++)
        {
            obm_count += CountBits32(OccupancyBitmaskVoxel[obm_addr + i]);
        }
        RWOccupancyBitmaskVoxel[unique_data_addr] = obm_count;// ユニークデータ部に占有ビット総数書き込み.



        // AOテスト.    
        const float3 voxel_pos_ws = (float3(voxel_coord) + 0.5) * cb_dispatch_param.CellSize + cb_dispatch_param.GridMinPos;

        float3 sample_ray_dir = random_unit_vector3(float2(voxel_pos_ws.x+voxel_pos_ws.y+voxel_pos_ws.z, (cb_dispatch_param.FrameCount)));
        // 埋まり回避のためカメラ方向にオフセットしてみる.
        const float3 sample_ray_origin = voxel_pos_ws + normalize(camera_pos - voxel_pos_ws) * cb_dispatch_param.CellSize*1.1;
        
        const float trace_distance = 10000.0;
        int hit_voxel_index = -1;
        float4 curr_ray_t_ws = trace_ray_vs_occupancy_bitmask_voxel(
            hit_voxel_index,
            sample_ray_origin, sample_ray_dir, trace_distance, 
            cb_dispatch_param.GridMinPos, cb_dispatch_param.CellSize, cb_dispatch_param.BaseResolution,
            cb_dispatch_param.GridToroidalOffset, OccupancyBitmaskVoxel);

        const uint voxel_gi_data = RWBufferWork[voxel_index];
        uint voxel_gi_sample_count = voxel_gi_data & 0xFFFF;
        uint voxel_gi_accumulated = (voxel_gi_data >> 16) & 0xFFFF;

        if(2000 <= voxel_gi_sample_count)
        {
            voxel_gi_sample_count = voxel_gi_sample_count/3;
            voxel_gi_accumulated = voxel_gi_accumulated/3;
        }
        voxel_gi_sample_count += 1;
        if(0.0 > curr_ray_t_ws.x)
        {
            voxel_gi_accumulated += 1;
        }

        const uint new_voxel_gi_data = (clamp(voxel_gi_accumulated, 0, 65535) << 16) | clamp(voxel_gi_sample_count, 0, 65535);
        RWBufferWork[voxel_index] = new_voxel_gi_data;
        
    }
}
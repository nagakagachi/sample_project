
#if 0

clear_voxel_cs.hlsl

#endif


#include "ssvg_util.hlsli"


// DepthBufferに対してDispatch.
[numthreads(128, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    uint voxel_count = cb_ssvg.base_grid_resolution.x * cb_ssvg.base_grid_resolution.y * cb_ssvg.base_grid_resolution.z;
    if(dtid.x < voxel_count)
    {
        RWCoarseVoxelBuffer[dtid.x] = (CoarseVoxelData)0; //empty_coarse_voxel_data();

        clear_voxel_data(RWOccupancyBitmaskVoxel, dtid.x);


        uint2 probe_2d_map_pos = uint2(dtid.x % cb_ssvg.probe_atlas_texture_base_width, dtid.x / cb_ssvg.probe_atlas_texture_base_width);
        for(int oct_i = 0; oct_i < k_probe_octmap_width_with_border; ++oct_i)
        {
            for(int oct_j = 0; oct_j < k_probe_octmap_width_with_border; ++oct_j)
            {
                RWTexProbeSkyVisibility[probe_2d_map_pos * k_probe_octmap_width_with_border + uint2(oct_i, oct_j)] = 0.0;
            }
        }
    }
}
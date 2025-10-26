
#if 0

bbv_clear_voxel_cs.hlsl

#endif


#include "ssvg_util.hlsli"


// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    uint voxel_count = cb_ssvg.bbv.grid_resolution.x * cb_ssvg.bbv.grid_resolution.y * cb_ssvg.bbv.grid_resolution.z;
    if(dtid.x < voxel_count)
    {
        RWBitmaskBrickVoxelOptionData[dtid.x] = (BbvOptionalData)0;

        clear_voxel_data(RWBitmaskBrickVoxel, dtid.x);

        {
            uint2 probe_2d_map_pos = uint2(dtid.x % cb_ssvg.bbv.flatten_2d_width, dtid.x / cb_ssvg.bbv.flatten_2d_width);
            for(int oct_j = 0; oct_j < k_probe_octmap_width_with_border; ++oct_j)
            {
                for(int oct_i = 0; oct_i < k_probe_octmap_width_with_border; ++oct_i)
                {
                    // ゼロクリア.
                    RWTexProbeSkyVisibility[probe_2d_map_pos * k_probe_octmap_width_with_border + uint2(oct_i, oct_j)] = 0.0;

                    #if 0
                        // デバッグ用にUVを書き込む.
                        if((0 != oct_i) && (k_probe_octmap_width_with_border-1 != oct_i) && (0 != oct_j) && (k_probe_octmap_width_with_border-1 != oct_j))
                        {
                            RWTexProbeSkyVisibility[probe_2d_map_pos * k_probe_octmap_width_with_border + uint2(oct_i, oct_j)] = (oct_i + oct_j * k_probe_octmap_width) / float(k_per_probe_texel_count);
                        }
                    #endif
                }
            }
        }
    }
}
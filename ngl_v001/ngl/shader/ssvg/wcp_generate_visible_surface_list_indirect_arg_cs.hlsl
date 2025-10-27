
#if 0

wcp_generate_visible_surface_list_indirect_arg_cs.hlsl

#endif


#include "ssvg_util.hlsli"

RWBuffer<uint>		RWVisibleSurfaceListIndirectArg;

// DepthBufferに対してDispatch.
[numthreads(1, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{

    const uint visible_surface_count = SurfaceProbeCellList[0];
    RWVisibleSurfaceListIndirectArg[0] = (visible_surface_count + (cb_ssvg.wcp_indirect_cs_thread_group_size.x - 1)) / cb_ssvg.wcp_indirect_cs_thread_group_size.x;
    RWVisibleSurfaceListIndirectArg[1] = 1;
    RWVisibleSurfaceListIndirectArg[2] = 1;

}
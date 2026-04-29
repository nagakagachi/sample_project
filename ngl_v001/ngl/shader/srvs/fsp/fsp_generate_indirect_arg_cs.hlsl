
#if 0

fsp_generate_indirect_arg_cs.hlsl

#endif


#include "../srvs_util.hlsli"

RWBuffer<uint>		RWFspIndirectArg;

// IndirectArg を 1 回だけ生成する.
[numthreads(1, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{

    const uint visible_surface_count = SurfaceProbeCellList[0];
    RWFspIndirectArg[0] = (visible_surface_count + (cb_srvs.fsp_indirect_cs_thread_group_size.x - 1)) / cb_srvs.fsp_indirect_cs_thread_group_size.x;
    RWFspIndirectArg[1] = 1;
    RWFspIndirectArg[2] = 1;

}

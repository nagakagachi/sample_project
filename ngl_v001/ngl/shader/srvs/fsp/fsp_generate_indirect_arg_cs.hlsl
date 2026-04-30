
#if 0

fsp_generate_indirect_arg_cs.hlsl

#endif


#include "../srvs_util.hlsli"

Buffer<uint>      ProbeIndexList;
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

    const uint list_count = ProbeIndexList[0];
    const uint dispatch_group_count = (list_count + (cb_srvs.fsp_indirect_cs_thread_group_size.x - 1)) / cb_srvs.fsp_indirect_cs_thread_group_size.x;
    RWFspIndirectArg[0] = max(dispatch_group_count, 1u);
    RWFspIndirectArg[1] = 1;
    RWFspIndirectArg[2] = 1;

}

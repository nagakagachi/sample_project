
#if 0

vsp_generate_indirect_arg_cs.hlsl

#endif


#include "../instant_rdv_util.hlsli"

Buffer<uint>      ProbeIndexList;
RWBuffer<uint>		RWVspIndirectArg;

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
    const uint dispatch_group_count = (list_count + (cb_instant_rdv.vsp_indirect_cs_thread_group_size.x - 1)) / cb_instant_rdv.vsp_indirect_cs_thread_group_size.x;
    RWVspIndirectArg[0] = max(dispatch_group_count, 1u);
    RWVspIndirectArg[1] = 1;
    RWVspIndirectArg[2] = 1;

}

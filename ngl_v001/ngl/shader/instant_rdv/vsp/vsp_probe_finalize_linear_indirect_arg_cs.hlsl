#if 0

vsp_probe_finalize_linear_indirect_arg_cs.hlsl

counter buffer[0] から 1D DispatchIndirect 引数を生成する。

#endif

#include "../instant_rdv_util.hlsli"

#define VSP_RAY_LINEAR_THREAD_GROUP_SIZE 128u

Buffer<uint> CounterBuffer;
RWBuffer<uint> RWLinearIndirectArg;

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint element_count = CounterBuffer[0];
    const uint dispatch_group_count =
        (element_count + (VSP_RAY_LINEAR_THREAD_GROUP_SIZE - 1u)) / VSP_RAY_LINEAR_THREAD_GROUP_SIZE;
    RWLinearIndirectArg[0] = max(dispatch_group_count, 1u);
    RWLinearIndirectArg[1] = 1u;
    RWLinearIndirectArg[2] = 1u;
}

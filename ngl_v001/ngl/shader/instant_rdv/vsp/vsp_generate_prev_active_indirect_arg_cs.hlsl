/*
    vsp_generate_prev_active_indirect_arg_cs.hlsl

    前フレームActiveProbeListの世代交代counterからBeginUpdate用の
    DispatchIndirect引数を生成する。

    ActiveProbeListは先頭2ワードを交互counterとして使用するため、
    SurfaceProbeCellList等の通常の[0] counter形式とは分けて扱う。
*/

#include "../instant_rdv_util.hlsli"

Buffer<uint> ProbeIndexList;
RWBuffer<uint> RWVspIndirectArg;

[numthreads(1, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    const uint list_count =
        ProbeIndexList[VspActiveProbePreviousCounterSlot()];
    const uint dispatch_group_count =
        (list_count + (cb_instant_rdv.vsp_indirect_cs_thread_group_size.x - 1u)) /
        cb_instant_rdv.vsp_indirect_cs_thread_group_size.x;

    RWVspIndirectArg[0] = max(dispatch_group_count, 1u);
    RWVspIndirectArg[1] = 1u;
    RWVspIndirectArg[2] = 1u;
}

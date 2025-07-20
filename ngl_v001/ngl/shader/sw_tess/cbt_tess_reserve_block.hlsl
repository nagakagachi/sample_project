// cbt_tess_reserve_block.hlsl
#include "cbt_tess_common.hlsli"
#define THREAD_GROUP_SIZE 128

// メモリブロックを予約するパス
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}

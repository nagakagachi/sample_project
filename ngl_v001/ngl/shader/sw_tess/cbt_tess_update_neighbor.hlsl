// cbt_tess_update_neighbor.hlsl
#include "cbt_tess_common.hlsli"
#define THREAD_GROUP_SIZE 128

// 隣接情報を更新するパス
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}

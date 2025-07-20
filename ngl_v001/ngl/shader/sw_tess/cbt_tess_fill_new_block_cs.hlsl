// cbt_tess_fill_new_block.hlsl
#include "cbt_tess_common.hlsli"

// 新しいブロックを生成して初期化するパス
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}


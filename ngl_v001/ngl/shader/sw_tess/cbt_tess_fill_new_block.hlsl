// cbt_tess_fill_new_block.hlsl
#include "cbt_tess_common.hlsli"

#define THREAD_GROUP_SIZE 128

// 新しいブロックを生成して初期化するパス
[numthreads(TESS_BLOCK_SIZE, 1, 1)]
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}

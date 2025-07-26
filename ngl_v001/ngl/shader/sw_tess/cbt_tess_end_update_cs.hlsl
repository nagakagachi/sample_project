// cbt_tess_end_update.hlsl
#include "cbt_tess_common.hlsli"

// 更新処理の完了フラグを設定するパス
[numthreads(1, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}


// cbt_tess_cache_index.hlsl
#include "cbt_tess_common.hlsli"

// バイセクタのインデックスをキャッシュに格納するパス
[numthreads(CBT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint thread_id = DTid.x;
}


#if 0

bbv_begin_view_update_cs.hlsl

BbvのView毎の処理の開始用処理. 除去リストのカウンタリセット.

#endif

#include "../srvs_util.hlsli"

[numthreads(1, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    if(0 == dtid.x)
    {
        RWRemoveVoxelList[0] = 0;
    }
}

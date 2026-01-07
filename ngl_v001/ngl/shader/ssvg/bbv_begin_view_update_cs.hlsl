
#if 0

bbv_begin_view_update_cs.hlsl

BbvのView毎の処理の開始用処理. ViewのDepthBufferから復元した表面Voxelを格納するリストのカウンタリセット.

#endif

#include "ssvg_util.hlsli"

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
        // VisibleCoarseVoxelListのアトミックカウンタをクリア.
        // 0番目はアトミックカウンタ用に予約している.
        RWVisibleVoxelList[0] = 0;

        RWRemoveVoxelList[0] = 0;
    }
}
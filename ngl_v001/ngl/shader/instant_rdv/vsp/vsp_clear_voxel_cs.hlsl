
#if 0

vsp_clear_voxel_cs.hlsl
ファイル説明: VSP lifecycle バッファと IrradianceVolume SH を初期化する。

#endif


#include "../instant_rdv_util.hlsli"

// DepthBufferに対してDispatch.
[numthreads(96, 1, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    // 全Voxelをクリア.
    const uint cell_count = cb_instant_rdv.vsp_total_cell_count;
    const uint probe_pool_size = cb_instant_rdv.vsp_probe_pool_size;

    if(0 == dtid.x)
    {
        RWSurfaceProbeCellList[0] = 0;
        RWVspProbeFreeStack[0] = probe_pool_size;
        // ActiveProbeListは先頭2ワードを世代交代counterとして使用する。
        RWVspActiveProbeListPrev[0] = 0;
        RWVspActiveProbeListPrev[1] = 0;
        RWVspActiveProbeListCurr[0] = 0;
        RWVspActiveProbeListCurr[1] = 0;
    }

    if(dtid.x < cell_count)
    {
        RWVspCellProbeIndexBuffer[dtid.x] = k_vsp_invalid_probe_index;

        // Dense IrradianceVolume SH は global cell index 直結の最終シェーディング参照先。
        VspIrradianceVolumeStoreSignals(
            dtid.x, 0.0.xxxx, 0.0.xxxx, 0.0.xxxx, 0.0.xxxx);
    }

    if(dtid.x < probe_pool_size)
    {
        VspProbePoolData probe_data = (VspProbePoolData)0;
        probe_data.owner_cell_index = k_vsp_invalid_probe_index;
        RWVspProbePoolBuffer[dtid.x] = probe_data;
        RWVspProbeFreeStack[dtid.x + 1] = probe_pool_size - 1 - dtid.x;

        const uint2 probe_2d_map_pos = VspProbeAtlasMapPos(dtid.x);
        [unroll]
        for(int oct_j = 0; oct_j < k_vsp_probe_octmap_width; ++oct_j)
        {
            [unroll]
            for(int oct_i = 0; oct_i < k_vsp_probe_octmap_width; ++oct_i)
            {
                RWVspProbeAtlasTex[probe_2d_map_pos * k_vsp_probe_octmap_width + uint2(oct_i, oct_j)] = 0.0.xxxx;
            }
        }
    }
}

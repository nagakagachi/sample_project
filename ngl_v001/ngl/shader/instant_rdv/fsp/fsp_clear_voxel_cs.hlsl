
#if 0

fsp_clear_voxel_cs.hlsl
ファイル説明: FSP lifecycle バッファと IrradianceVolume SH を初期化する。

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
    const uint cell_count = cb_instant_rdv.fsp_total_cell_count;
    const uint probe_pool_size = cb_instant_rdv.fsp_probe_pool_size;

    if(0 == dtid.x)
    {
        RWSurfaceProbeCellList[0] = 0;
        RWFspProbeFreeStack[0] = probe_pool_size;
        // ActiveProbeListは先頭2ワードを世代交代counterとして使用する。
        RWFspActiveProbeListPrev[0] = 0;
        RWFspActiveProbeListPrev[1] = 0;
        RWFspActiveProbeListCurr[0] = 0;
        RWFspActiveProbeListCurr[1] = 0;
    }

    if(dtid.x < cell_count)
    {
        RWFspCellProbeIndexBuffer[dtid.x] = k_fsp_invalid_probe_index;

        // Dense IrradianceVolume SH は global cell index 直結の最終シェーディング参照先。
        FspIrradianceVolumeStorePackedCoeffs(
            dtid.x, 0.0.xxxx, 0.0.xxxx, 0.0.xxxx, 0.0.xxxx);
    }

    if(dtid.x < probe_pool_size)
    {
        FspProbePoolData probe_data = (FspProbePoolData)0;
        probe_data.owner_cell_index = k_fsp_invalid_probe_index;
        RWFspProbePoolBuffer[dtid.x] = probe_data;
        RWFspProbeFreeStack[dtid.x + 1] = probe_pool_size - 1 - dtid.x;

        const uint2 probe_2d_map_pos = FspProbeAtlasMapPos(dtid.x);
        [unroll]
        for(int oct_j = 0; oct_j < k_fsp_probe_octmap_width; ++oct_j)
        {
            [unroll]
            for(int oct_i = 0; oct_i < k_fsp_probe_octmap_width; ++oct_i)
            {
                RWFspProbeAtlasTex[probe_2d_map_pos * k_fsp_probe_octmap_width + uint2(oct_i, oct_j)] = 0.0.xxxx;
            }
        }
    }
}

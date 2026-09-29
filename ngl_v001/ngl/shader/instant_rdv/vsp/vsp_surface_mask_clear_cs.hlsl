#if 0
vsp_surface_mask_clear_cs.hlsl

VSP SurfaceCellMaskのクリアパス。
Cascadeごとの8x8x8 Brick bitmaskを毎フレーム0初期化する。

SurfaceCellMask処理の先頭パス。
検出マスクは前フレームのbitを再利用できないため毎フレームclearする。
Buffer clear APIではなくcomputeで実行しているのは、既存のVSP dispatch/marker列に
揃えて可視化しやすくし、将来必要なら部分clearへ拡張しやすくするため。
#endif

#define VSP_SURFACE_MASK_CLEAR_THREAD_GROUP_SIZE 256

#include "../instant_rdv_util.hlsli"

[numthreads(VSP_SURFACE_MASK_CLEAR_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint mask_word_count = VspSurfaceMaskWordCount();
    if(dtid.x >= mask_word_count)
    {
        return;
    }

    // 1 thread clears 1 uint word, which covers 32 global VSP cells.
    // The buffer is compact enough that this cost is expected to be much smaller
    // than the depth-driven injection and later probe update work.
    RWVspSurfaceCellMaskBuffer[dtid.x] = 0u;
}

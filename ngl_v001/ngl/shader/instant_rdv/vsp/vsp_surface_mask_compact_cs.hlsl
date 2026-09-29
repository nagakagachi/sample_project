#if 0
vsp_surface_mask_compact_cs.hlsl

VSP SurfaceCellMaskのコンパクションパス。
立っているbitを SurfaceProbeCellList へ詰め、既存VSP更新パスへ合流させる。
#endif

#define VSP_SURFACE_MASK_COMPACT_THREAD_GROUP_SIZE 128

#include "../instant_rdv_util.hlsli"

[numthreads(VSP_SURFACE_MASK_COMPACT_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint total_cell_count = (uint)max(cb_instant_rdv.vsp_total_cell_count, 0);
    const uint mask_word_count = VspSurfaceMaskWordCount();
    const uint mask_word_index = dtid.x;
    if(mask_word_index >= mask_word_count)
    {
        return;
    }

    const uint mask_bits = VspSurfaceCellMaskBuffer[mask_word_index];
    if(mask_bits == 0u)
    {
        return;
    }

    // Reserve contiguous output slots once per non-zero word. This keeps append
    // atomics proportional to non-empty mask words rather than set bits or pixels.
    const uint bit_count = countbits(mask_bits);
    uint append_base_index = 0u;
    InterlockedAdd(RWSurfaceProbeCellList[0], bit_count, append_base_index);

    // Match the existing visible surface list contract:
    // element 0 is a counter and elements [1, max_visible_cell_count] are cells.
    // If the reservation overflowed, undo only the unwritable part so later passes
    // see a bounded count instead of reading past the allocated list.
    const uint max_visible_cell_count = (uint)max(cb_instant_rdv.vsp_visible_voxel_buffer_size, 0);
    uint writable_count = 0u;
    if(append_base_index < max_visible_cell_count)
    {
        writable_count = min(bit_count, max_visible_cell_count - append_base_index);
    }
    const uint overflow_count = bit_count - writable_count;
    if(overflow_count > 0u)
    {
        InterlockedAdd(RWSurfaceProbeCellList[0], -(int)overflow_count);
    }

    uint remaining_bits = mask_bits;
    uint local_rank = 0u;
    while(remaining_bits != 0u)
    {
        // Iterate only set bits. firstbitlow + bit clear avoids scanning all 32
        // bit positions when a word contains only a few visible cells.
        const uint bit_index = firstbitlow(remaining_bits);
        remaining_bits &= (remaining_bits - 1u);

        // Preserve the VSP X-major global address; no Morton conversion belongs on this path.
        uint global_cell_index = k_vsp_invalid_probe_index;
        if(!VspGetGlobalCellIndexFromSurfaceMaskBit(
            mask_word_index,
            bit_index,
            global_cell_index) ||
           global_cell_index >= total_cell_count)
        {
            continue;
        }

        if(local_rank < writable_count)
        {
            RWSurfaceProbeCellList[append_base_index + local_rank + 1u] = global_cell_index;
        }
        ++local_rank;
    }
}

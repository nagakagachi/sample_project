#if 0

ss_probe_direct_sh_preupdate_cs.hlsl

DirectSH方式専用 ProbeタイルTileInfo 更新.
既存の ss_probe_preupdate_cs.hlsl と同ロジックだが,
入出力先を DirectSH専用テクスチャ (ScreenSpaceProbeDirectSHTileInfoTex) に変更している.

#endif

#include "srvs_util.hlsli"
#include "../include/scene_view_struct.hlsli"
#include "../include/depth_buffer_util.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D                     TexHardwareDepth;

#define DISPATCH_GROUP_SIZE_X SCREEN_SPACE_PROBE_TILE_SIZE
#define DISPATCH_GROUP_SIZE_Y SCREEN_SPACE_PROBE_TILE_SIZE

[numthreads(DISPATCH_GROUP_SIZE_X, DISPATCH_GROUP_SIZE_Y, 1)]
void main_cs(
    uint3 dtid   : SV_DispatchThreadID,
    uint3 gtid   : SV_GroupThreadID,
    uint3 gid    : SV_GroupID
)
{
    const int2 probe_id = dtid.xy;

    RandomInstance rng;
    rng.rngState = asuint(noise_float_to_float(float3(probe_id.x, probe_id.y, cb_srvs.frame_count)));

    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);

    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    uint2 probe_pos_in_tile = uint2(0, 0);
    int2  current_probe_texel_pos = ss_probe_tile_pixel_start;
    float probe_depth = 1.0;
    {
        // 前フレームの DirectSH 専用 TileInfo から位置を復元.
        const float4 prev_info = ScreenSpaceProbeDirectSHHistoryTileInfoTex[probe_id];

        probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(prev_info.y);
        current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
        probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;

        const float relocation_probability = cb_srvs.ss_probe_preupdate_relocation_probability;
        if(!isValidDepth(probe_depth) || (relocation_probability > rng.rand()))
        {
            for(int i = 0; i < SCREEN_SPACE_PROBE_TILE_SIZE; ++i)
            {
                probe_pos_in_tile = rng.rand2() * (SCREEN_SPACE_PROBE_TILE_SIZE - 1);
                current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
                probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
                if(isValidDepth(probe_depth))
                    break;
            }
        }
    }

    if(isValidDepth(probe_depth))
    {
        const float2 probe_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;
        const float3 pixel_pos_vs = CalcViewSpacePosition(probe_uv, calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

        const float3 approx_normal_vs = reconstruct_normal_vs_fine(TexHardwareDepth, current_probe_texel_pos, probe_depth, depth_size_inv, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef, cb_ngl_sceneview.cb_proj_mtx);
        const float3 approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);

        const float2 approx_normal_oct = OctEncode(approx_normal_ws);
        RWScreenSpaceProbeDirectSHTileInfoTex[probe_id] = SspTileInfoBuild(probe_depth, probe_pos_in_tile, approx_normal_oct, false);
    }
    else
    {
        RWScreenSpaceProbeDirectSHTileInfoTex[probe_id] = SspTileInfoBuild(1.0, uint2(0, 0), float2(0.0, 0.0), false);
    }
}

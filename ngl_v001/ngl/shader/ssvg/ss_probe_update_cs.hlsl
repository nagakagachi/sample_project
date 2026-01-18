
#if 0

ss_probe_clear_cs.hlsl

ScreenSpaceProbe用テクスチャクリア.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			           TexHardwareDepth;

#define SCREEN_SPACE_PROBE_TILE_SIZE 8

groupshared float tile_hw_depth;
groupshared float tile_view_z;
groupshared float3 tile_pos_vs;
groupshared float3 tile_pos_ws;

[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    //cb_ssvg.frame_count;
    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    
    const float2 screen_uv = (float2(dtid.xy) + float2(0.5, 0.5)) / float2(depth_size);// ピクセル中心への半ピクセルオフセット考慮.


    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = gid.xy;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    const uint frame_rand = noise_iqint32_orig(gid.xy + cb_ssvg.frame_count);
    const uint2 rand_element_in_tile = uint2(frame_rand / SCREEN_SPACE_PROBE_TILE_SIZE, frame_rand) % SCREEN_SPACE_PROBE_TILE_SIZE;

    const int2 rand_texel_pos = ss_probe_tile_pixel_start + rand_element_in_tile;
    const float2 rand_texel_uv = (float2(rand_texel_pos) + float2(0.5, 0.5)) / float2(depth_size);// ピクセル中心への半ピクセルオフセット考慮.
    
    if(all(gtid.xy == 0))
    {

        const float d = TexHardwareDepth.Load(int3(rand_texel_pos, 0)).r;
        if(0.0 < d && d < 1.0)
        {
            const float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pixel_pos_vs = CalcViewSpacePosition(rand_texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

            tile_hw_depth = d;
            tile_view_z = view_z;
            tile_pos_vs = pixel_pos_vs;
            tile_pos_ws = pixel_pos_ws;
        }
        else
        {
            tile_hw_depth = 1.0;
            tile_view_z = 0.0;
            tile_pos_vs = float3(0.0, 0.0, 0.0);
            tile_pos_ws = float3(0.0, 0.0, 0.0);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 仮書き込み.
    RWScreenSpaceProbeTex[dtid.xy] = ( RWScreenSpaceProbeTex[dtid.xy] +float4(tile_pos_ws, 0.0)) * 0.5;
}
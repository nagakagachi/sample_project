
#if 0

ss_probe_preupdate_cs.hlsl

ScreenSpaceProbe ProbeTile用の情報更新.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D			           TexHardwareDepth;

// -------------------------------------

bool isValidDepth(float d)
{
    // 
    return (0.0 < d && d < 1.0);
}

#define DISPATCH_GROUP_SIZE_X SCREEN_SPACE_PROBE_TILE_SIZE
#define DISPATCH_GROUP_SIZE_Y SCREEN_SPACE_PROBE_TILE_SIZE

[numthreads(DISPATCH_GROUP_SIZE_X, DISPATCH_GROUP_SIZE_Y, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID
)
{
    const int2 probe_id = dtid.xy;// フル解像度に対して 1/SCREEN_SPACE_PROBE_TILE_SIZE で, ScreenSpaceProbeごとに1テクセル.
    
    uint2 depth_size;
    TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    const float2 depth_size_inv = 1.0 / float2(depth_size);
    

    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;

    // Tile内探索.
    uint2 probe_pos_in_tile = uint2(0,0);
    int2 current_probe_texel_pos = ss_probe_tile_pixel_start;
    float probe_depth = 1.0;
    #if 1
        // 前回情報
        const float4 prev_probe_info = RWScreenSpaceProbeTileInfoTex[probe_id];

        uint select_probe_pos_index = uint(prev_probe_info.y);
        // 前回が失敗だった場合, または一定の確率でランダム選択.
        const float re_select_threshold = 0.5;// 再選択確率.
        if(!isValidDepth(prev_probe_info.x) || re_select_threshold > noise_float_to_float(float3(asfloat(ss_probe_tile_pixel_start.x), asfloat(ss_probe_tile_pixel_start.y), asfloat(cb_ssvg.frame_count))))
        {
            select_probe_pos_index += 1;// 少しずらす(適当).
        }

        // 何回かリトライする.
        for(int i = 0; i < SCREEN_SPACE_PROBE_TILE_SIZE; ++i)
        {
            probe_pos_in_tile = uint2(select_probe_pos_index * SCREEN_SPACE_PROBE_TILE_SIZE_INV, select_probe_pos_index) % SCREEN_SPACE_PROBE_TILE_SIZE;
            // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
            current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
            // プローブの配置テクセルの深度取得.
            probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
            if(isValidDepth(probe_depth))
                break;

            // 次のセルを選択(ランダム).
            select_probe_pos_index = hash_uint32_iq(probe_id * select_probe_pos_index + (cb_ssvg.frame_count ^ probe_id));
        }
    #else
        const uint select_probe_pos_index = hash_uint32_iq(probe_id + (probe_id ^ cb_ssvg.frame_count));
        probe_pos_in_tile = uint2(select_probe_pos_index * SCREEN_SPACE_PROBE_TILE_SIZE_INV, select_probe_pos_index) % SCREEN_SPACE_PROBE_TILE_SIZE;
        // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
        current_probe_texel_pos = ss_probe_tile_pixel_start + probe_pos_in_tile;
        // プローブの配置テクセルの深度取得.
        probe_depth = TexHardwareDepth.Load(int3(current_probe_texel_pos, 0)).r;
    #endif

    if(isValidDepth(probe_depth))
    {
        const float2 probe_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;
        const float3 pixel_pos_vs = CalcViewSpacePosition(probe_uv, calc_view_z_from_ndc_z(probe_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

        // 近傍深度から簡易法線推定.
        const int2 depth_size_i = int2(depth_size) - 1;
        const int2 xn_pos = clamp(current_probe_texel_pos + int2(-1, 0), int2(0, 0), depth_size_i);
        const int2 xp_pos = clamp(current_probe_texel_pos + int2( 1, 0), int2(0, 0), depth_size_i);
        const int2 yn_pos = clamp(current_probe_texel_pos + int2(0, -1), int2(0, 0), depth_size_i);
        const int2 yp_pos = clamp(current_probe_texel_pos + int2(0,  1), int2(0, 0), depth_size_i);

        float xn_depth = TexHardwareDepth.Load(int3(xn_pos, 0)).r;
        float xp_depth = TexHardwareDepth.Load(int3(xp_pos, 0)).r;
        float yn_depth = TexHardwareDepth.Load(int3(yn_pos, 0)).r;
        float yp_depth = TexHardwareDepth.Load(int3(yp_pos, 0)).r;

        if(!isValidDepth(xn_depth)) { xn_depth = probe_depth; }
        if(!isValidDepth(xp_depth)) { xp_depth = probe_depth; }
        if(!isValidDepth(yn_depth)) { yn_depth = probe_depth; }
        if(!isValidDepth(yp_depth)) { yp_depth = probe_depth; }

        const float3 xn_pos_vs = CalcViewSpacePosition((float2(xn_pos) + 0.5) * depth_size_inv, calc_view_z_from_ndc_z(xn_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 xp_pos_vs = CalcViewSpacePosition((float2(xp_pos) + 0.5) * depth_size_inv, calc_view_z_from_ndc_z(xp_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 yn_pos_vs = CalcViewSpacePosition((float2(yn_pos) + 0.5) * depth_size_inv, calc_view_z_from_ndc_z(yn_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
        const float3 yp_pos_vs = CalcViewSpacePosition((float2(yp_pos) + 0.5) * depth_size_inv, calc_view_z_from_ndc_z(yp_depth, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);

        float3 approx_normal_vs = cross(xp_pos_vs - xn_pos_vs, yp_pos_vs - yn_pos_vs);
        float3 approx_normal_ws = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
        if(dot(abs(approx_normal_vs), float3(1.0, 1.0, 1.0)) > 1e-6)
        {
            approx_normal_vs = normalize(approx_normal_vs);
            approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);
        }
        
        // タイル内のプローブ位置をフラットインデックス化.
        const int probe_pos_flat_index_in_tile = probe_pos_in_tile.y * SCREEN_SPACE_PROBE_TILE_SIZE + probe_pos_in_tile.x;
        // 配置できたらその情報を格納.
        const float2 approx_normal_oct = OctEncode(normalize(approx_normal_ws));
        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(probe_depth, probe_pos_flat_index_in_tile, approx_normal_oct.x, approx_normal_oct.y);
    }
    else
    {
        RWScreenSpaceProbeTileInfoTex[probe_id] = float4(1.0, 0, 0, 0);// 配置できなかった場合はDepthに負の値を入れておくなどして無効化.
    }

}
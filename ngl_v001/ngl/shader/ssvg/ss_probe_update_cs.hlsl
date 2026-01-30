
#if 0

ss_probe_update_cs.hlsl

ScreenSpaceProbe更新.

#endif

#include "ssvg_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
//Texture2D			           TexHardwareDepth;


// -------------------------------------
// Tile ThreadGroup共有のため共有メモリ.

// TileのこのフレームでのScreenSpaceProbe配置テクセル情報.
groupshared float ss_probe_hw_depth;
groupshared float ss_probe_view_z;
groupshared float3 ss_probe_pos_vs;
groupshared float3 ss_probe_pos_ws;

// ScreenSpaceProbe配置位置の近似法線情報.
groupshared float3 ss_probe_approx_normal_ws;

// -------------------------------------

bool isValidDepth(float d)
{
    // 
    return (0.0 < d && d < 1.0);
}


[numthreads(SCREEN_SPACE_PROBE_TILE_SIZE, SCREEN_SPACE_PROBE_TILE_SIZE, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID
)
{
    const int2 frame_skip_probe_offset = 
    int2(cb_ssvg.frame_count % cb_ssvg.screen_probe_temporal_update_tile_size, (cb_ssvg.frame_count / cb_ssvg.screen_probe_temporal_update_tile_size) % cb_ssvg.screen_probe_temporal_update_tile_size);

    const int2 probe_atlas_local_pos = gtid.xy;// タイル内でのローカル位置は時間分散でスキップされないのでそのまま.
    const int2 probe_id = gid.xy * cb_ssvg.screen_probe_temporal_update_tile_size + frame_skip_probe_offset;// プローブフレームスキップ考慮.
    const int2 global_pos = probe_id * SCREEN_SPACE_PROBE_TILE_SIZE + probe_atlas_local_pos;// グローバルテクセル位置計算.
    
    const uint frame_rand = hash_uint32_iq(probe_id + cb_ssvg.frame_count);


    // Tile内で今回処理するテクセルを決定して最小限のテクスチャ読み取り.
    const int2 ss_probe_tile_id = probe_id;
    const int2 ss_probe_tile_pixel_start = ss_probe_tile_id * SCREEN_SPACE_PROBE_TILE_SIZE;


    // ScreenSpaceProbeTexelTile情報テクスチャから取得.
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id, 0));
    const float ss_probe_depth = ss_probe_tile_info.x;
    const int2 ss_probe_pos_rand_in_tile = int2(int(ss_probe_tile_info.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ss_probe_tile_info.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
    const float ss_candidate_hit_t = ss_probe_tile_info.w;


    //uint2 depth_size;
    //TexHardwareDepth.GetDimensions(depth_size.x, depth_size.y);
    //const float2 depth_size_inv = 1.0 / float2(depth_size);
    uint2 depth_size = cb_ngl_sceneview.cb_render_resolution;
    const float2 depth_size_inv = cb_ngl_sceneview.cb_render_resolution_inv;
    
    // このフレームでのプローブ配置テクセル位置をタイル内ランダム選択.
    const int2 current_probe_texel_pos = ss_probe_tile_pixel_start + ss_probe_pos_rand_in_tile;
    const float2 current_probe_texel_uv = (float2(current_probe_texel_pos) + float2(0.5, 0.5)) * depth_size_inv;// ピクセル中心への半ピクセルオフセット考慮.
    
    // タイルのプローブ配置情報を代表して取得.
    if(all(probe_atlas_local_pos == 0))
    {
        // クリア
        ss_probe_hw_depth = 1.0;
        ss_probe_view_z = 0.0;
        ss_probe_pos_vs = float3(0.0, 0.0, 0.0);
        ss_probe_pos_ws = float3(0.0, 0.0, 0.0);
        ss_probe_approx_normal_ws = float3(0.0, 0.0, 0.0);


        // プローブのレイ始点とするピクセルの深度取得.
        const float d = ss_probe_depth;

        if(0.0 <= ss_candidate_hit_t)
        {
            const float view_z = calc_view_z_from_ndc_z(d, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
            const float3 pixel_pos_vs = CalcViewSpacePosition(current_probe_texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);
            const float3 pixel_pos_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(pixel_pos_vs, 1.0));

            #if 1
                float2 neighbor_probe_depth_x = float2(1.0, 1.0);
                int2 neighbor_probe_global_pos_x[2];
                float2 neighbor_probe_depth_y = float2(1.0, 1.0);
                int2 neighbor_probe_global_pos_y[2];
                for(int offset_i = 0; offset_i < 1; ++offset_i)
                {
                    const int offset_base = offset_i + 1;

                    // X方向
                    for(int xi = 0; xi < 2; ++xi)
                    {
                        if(!isValidDepth(neighbor_probe_depth_x[xi]))
                        {
                            const int offset = offset_base * (xi * 2 - 1);
                            const float4 ssp_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id + int2(offset, 0), 0));
                            const int2 ssp_probe_pos_rand_in_tile = int2(int(ssp_info.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ssp_info.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
                            
                            neighbor_probe_depth_x[xi] = ssp_info.x;
                            neighbor_probe_global_pos_x[xi] = (ss_probe_tile_id + int2(offset, 0)) * SCREEN_SPACE_PROBE_TILE_SIZE + ssp_probe_pos_rand_in_tile;
                        }
                    }
                    // Y方向
                    for(int yi = 0; yi < 2; ++yi)
                    {
                        if(!isValidDepth(neighbor_probe_depth_y[yi]))
                        {
                            const int offset = offset_base * (yi * 2 - 1);
                            const float4 ssp_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id + int2(0, offset), 0));
                            const int2 ssp_probe_pos_rand_in_tile = int2(int(ssp_info.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ssp_info.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
                        
                            neighbor_probe_depth_y[yi] = ssp_info.x;
                            neighbor_probe_global_pos_y[yi] = (ss_probe_tile_id + int2(0, offset)) * SCREEN_SPACE_PROBE_TILE_SIZE + ssp_probe_pos_rand_in_tile;
                        }
                    }
                }

                // XYそれぞれdepth有効な要素がとれなかったらセンターのデプスで埋める. 座標は近傍位置のままとする.
                for(int ni = 0; ni < 1; ++ni)
                {
                    if(!isValidDepth(neighbor_probe_depth_x[ni]))
                    {
                        neighbor_probe_depth_x[ni] = d;
                    }
                    if(!isValidDepth(neighbor_probe_depth_y[ni]))
                    {
                        neighbor_probe_depth_y[ni] = d;
                    }
                }
                // 近傍情報から法線近似.
                float3 approx_normal_ws = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);// 法線デフォルトはカメラ方向の逆.
                {
                    const float3 xn_pixel_pos_vs = CalcViewSpacePosition((neighbor_probe_global_pos_x[0] + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(neighbor_probe_depth_x[0], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
                    const float3 xp_pixel_pos_vs = CalcViewSpacePosition((neighbor_probe_global_pos_x[1] + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(neighbor_probe_depth_x[1], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
                    const float3 yn_pixel_pos_vs = CalcViewSpacePosition((neighbor_probe_global_pos_y[0] + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(neighbor_probe_depth_y[0], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
                    const float3 yp_pixel_pos_vs = CalcViewSpacePosition((neighbor_probe_global_pos_y[1] + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(neighbor_probe_depth_y[1], cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);

                    // nとpで中央差分による法線計算.
                    const float3 approx_normal_vs = normalize(cross(xp_pixel_pos_vs - xn_pixel_pos_vs, yp_pixel_pos_vs - yn_pixel_pos_vs));
                    approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);
                }

            #else
                // 近傍Probe情報を取得して法線近似するなど.
                const float4 ss_probe_tile_info_nx = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id + int2(1, 0), 0));
                const float4 ss_probe_tile_info_ny = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id + int2(0, 1), 0));
                const float d_x = ss_probe_tile_info_nx.x;
                const float d_y = ss_probe_tile_info_ny.x;
                const int2 ss_probe_global_pos_nx = (ss_probe_tile_id + int2(1, 0)) * SCREEN_SPACE_PROBE_TILE_SIZE + int2(int(ss_probe_tile_info_nx.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ss_probe_tile_info_nx.y) / SCREEN_SPACE_PROBE_TILE_SIZE);
                const int2 ss_probe_global_pos_ny = (ss_probe_tile_id + int2(0, 1)) * SCREEN_SPACE_PROBE_TILE_SIZE + int2(int(ss_probe_tile_info_ny.y) % SCREEN_SPACE_PROBE_TILE_SIZE, int(ss_probe_tile_info_ny.y) / SCREEN_SPACE_PROBE_TILE_SIZE);

                float3 approx_normal_ws = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);// 法線デフォルトはカメラ方向の逆.
                if(0.0 <= ss_probe_tile_info_nx.w && 0.0 <= ss_probe_tile_info_ny.w)
                {
                    const float3 nx_pixel_pos_vs = CalcViewSpacePosition((ss_probe_global_pos_nx + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(d_x, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);
                    const float3 ny_pixel_pos_vs = CalcViewSpacePosition((ss_probe_global_pos_ny + 0.5)*depth_size_inv, calc_view_z_from_ndc_z(d_y, cb_ngl_sceneview.cb_ndc_z_to_view_z_coef), cb_ngl_sceneview.cb_proj_mtx);

                    const float3 approx_normal_vs = normalize(cross(nx_pixel_pos_vs - pixel_pos_vs, ny_pixel_pos_vs - pixel_pos_vs));
                    approx_normal_ws = mul((float3x3)cb_ngl_sceneview.cb_view_inv_mtx, approx_normal_vs);
                }
            #endif

            // タイル共有情報.
            {
                ss_probe_hw_depth = d;
                ss_probe_view_z = view_z;
                ss_probe_pos_vs = pixel_pos_vs;
                ss_probe_pos_ws = pixel_pos_ws;

                ss_probe_approx_normal_ws = approx_normal_ws;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();// タイル代表情報の共有メモリ書き込み待ち.


    // この後さらに共有メモリでバリアする場合などはリターンできなくなるかも.
    if(1.0 <= ss_probe_hw_depth)
    {
        // ミスタイルはクリアすべきか, 近傍SSプローブやワールドプローブで補填すべきか.
        //RWScreenSpaceProbeTex[global_pos] = float4(0.0, 0.0, 0.0, 0.0);
        RWScreenSpaceProbeTex[global_pos].w = 0.0;// ミスタイルはw成分だけクリアして他は残しておく.
        return;
    }

    // カメラ座標.
    const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);

    #if 0
        // 担当アトラステクセルのOctahedral方向の固定レイ方向.
        const float3 sample_ray_dir = OctDecode(float2(probe_atlas_local_pos + 0.5)*SCREEN_SPACE_PROBE_TILE_SIZE_INV);
    #else
        // OctMapセル毎にレイを発行. セル内でJitter.
        const float2 noise_float2 = noise_float3_to_float2(float3(global_pos.xy, float(frame_rand))) * 2.0 - 1.0;
        const float3 sample_ray_dir = OctDecode(((float2(probe_atlas_local_pos) + 0.5 + noise_float2*0.5)*SCREEN_SPACE_PROBE_TILE_SIZE_INV));
    #endif

    // 自己遮蔽回避の位置オフセット.
    const float ray_origin_view_offset_scale = cb_ssvg.bbv.cell_size * k_bbv_per_voxel_resolution_inv * 0.5;
    const float ray_origin_normal_offset_scale = cb_ssvg.bbv.cell_size * k_bbv_per_voxel_resolution_inv * 0.5;
    const float3 sample_ray_origin = ss_probe_pos_ws + normalize(view_origin - ss_probe_pos_ws)*ray_origin_view_offset_scale + ss_probe_approx_normal_ws*ray_origin_normal_offset_scale;
    
    //const float ray_origin_view_offset_scale = ss_candidate_hit_t;// 前処理で表面からVoxelの外に出るための距離をトレースしている場合.
    //const float ray_origin_normal_offset_scale = 0.1;
    //const float3 sample_ray_origin = ss_probe_pos_ws + normalize(view_origin - ss_probe_pos_ws)*ray_origin_view_offset_scale + ss_probe_approx_normal_ws * ray_origin_normal_offset_scale;
    
    // 自己遮蔽回避の初期ヒット無視回数. 単位はBrick内BitCell.
    const int initial_hit_avoidance_count = 1;

    // タイルのスクリーンスペースプローブ位置から, タイル内スレッド毎にレイトレース.
    const float trace_distance = 30.0;
    int hit_voxel_index = -1;
    float4 debug_ray_info;
    float4 curr_ray_t_ws = 
    trace_bbv_initial_hit_avoidance
    (
        hit_voxel_index, debug_ray_info,
        sample_ray_origin, sample_ray_dir, trace_distance, 
        cb_ssvg.bbv.grid_min_pos, cb_ssvg.bbv.cell_size, cb_ssvg.bbv.grid_resolution,
        cb_ssvg.bbv.grid_toroidal_offset, BitmaskBrickVoxel, initial_hit_avoidance_count);

    // ヒットしなかったら空が見えているものとしてその方向を格納.
    const float3 hit_debug = (0.0 > curr_ray_t_ws.x)? sample_ray_dir : 0.0;

    // デバッグ approx_normal_ws
    //const float3 hit_debug = (0.0 > curr_ray_t_ws.x)? ss_probe_approx_normal_ws : 0.0;
    //const float3 hit_debug = ss_probe_approx_normal_ws;

    // 仮書き込み.
    const float temporal_rate = 0.01;
    RWScreenSpaceProbeTex[global_pos] = lerp( RWScreenSpaceProbeTex[global_pos], float4(hit_debug, (0.0 > curr_ray_t_ws.x)? 1.0 : 0.0), temporal_rate);
}
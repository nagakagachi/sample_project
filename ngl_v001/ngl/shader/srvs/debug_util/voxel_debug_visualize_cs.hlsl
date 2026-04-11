
#if 0

ss_voxel_debug_visualize_cs.hlsl

デバッグ可視化.

#endif


#include "../srvs_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

RWTexture2D<float4>	RWTexWork;
SamplerState		SmpLinearClamp;

float debug_count_to_rate(float count)
{
    return count / (count + 4.0);
}


// デバッグテクスチャに対してDispatch.
[numthreads(16, 16, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_srvs.tex_main_view_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);
    const int2 texel_pos = dtid.xy;
    
	const float3 camera_dir = GetViewDirFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    
    const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, cb_ngl_sceneview.cb_proj_mtx);
    const float3 ray_dir_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));


    // ScreenSpaceProbe Octahedral Map.
    const int2 ss_probe_tile_id = int2(floor(float2(texel_pos) / SCREEN_SPACE_PROBE_INFO_DOWNSCALE));
    const int2 ss_probe_screen_tile_base_pos = ss_probe_tile_id * SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
    const int2 ss_probe_atlas_tile_base_pos = ss_probe_tile_id * SCREEN_SPACE_PROBE_OCT_RESOLUTION;
    const float4 ss_probe_tile_info = ScreenSpaceProbeTileInfoTex.Load(int3(ss_probe_tile_id, 0));
    const float ss_probe_depth = ss_probe_tile_info.x;
    const float3 ss_probe_tile_normal_ws = OctDecode(ss_probe_tile_info.zw);
    const int2 ss_probe_pos_in_tile = SspTileInfoDecodeProbePosInTile(ss_probe_tile_info.y);
    const bool ss_probe_reprojection_succeeded = SspTileInfoIsReprojectionSucceeded(ss_probe_tile_info.y);


    const int debug_category = cb_srvs.debug_view_category;
    const int debug_sub_mode = cb_srvs.debug_view_sub_mode;

    // Category 0: BBV.
    if(0 == debug_category)
    {
        if(4 >= debug_sub_mode)
        {
            // Voxel単位Traceのテスト.
            const float trace_distance = 10000.0;          
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance, 
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float fog_rate0 = pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2);
                const float fog_rate1 = saturate((curr_ray_t_ws.x - 70.0)/500.0);

                const uint brick_occupied_voxel_count = BitmaskBrickVoxel[bbv_voxel_coarse_occupancy_info_addr(hit_voxel_index)];
                // デバッグ用テクスチャにモード別描画.
                if(0 == debug_sub_mode)
                {
                    // bbvセル可視化
                    const float3 bbv_cell_id = floor((view_origin + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_srvs.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                    debug_color.xyz = float4(noise_float_to_float(bbv_cell_id.xyzz), noise_float_to_float(bbv_cell_id.xzyy), noise_float_to_float(bbv_cell_id.xyzx), 1);

                    // 簡易フォグ.
                    debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                    debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
                }
                else if(1 == debug_sub_mode)
                {
                    // VoxelIDを可視化.
                    debug_color.xyz = float4(noise_float_to_float(hit_voxel_index), noise_float_to_float(hit_voxel_index*2), noise_float_to_float(hit_voxel_index*3), 1);
                    
                    // 簡易フォグ.
                    debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                    debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
                }
                else if(2 == debug_sub_mode)
                {
                    // Bbvセルのヒット法線可視化.
                    const float3 bbv_cell_id = floor((view_origin + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_srvs.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                    debug_color.xyz = abs(curr_ray_t_ws.yzw);
                    
                    // 簡易フォグ.
                    debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                    debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
                }
                else if(3 == debug_sub_mode)
                {
                    // Bbvセルの深度を可視化.
                    debug_color.xyz = float4(saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0), 1);
                }
                else if(4 == debug_sub_mode)
                {
                    const float count_rate = saturate(float(brick_occupied_voxel_count) / float(k_bbv_per_voxel_bitmask_bit_count));
                    debug_color.xyz = lerp(float3(0.0, 0.0, 0.1), float3(1.0, 0.8, 0.2), count_rate);
                }
            }
            RWTexWork[dtid.xy] = debug_color;
        }
        else if(5 == debug_sub_mode)
        {
            // Brick単位Traceのテスト. Brickの占有フラグが適切に設定または除去されているかのテスト.
            const float trace_distance = 10000.0;          
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance, 
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, true);
                
            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                // VoxelIDを可視化.
                debug_color.xyz = float4(noise_float_to_float(hit_voxel_index), noise_float_to_float(hit_voxel_index*2), noise_float_to_float(hit_voxel_index*3), 1);
                
                // 簡易フォグ.
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2) * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), saturate((curr_ray_t_ws.x - 70.0)/500.0) * 0.8);
            }
            RWTexWork[dtid.xy] = debug_color;
        }
        else if(6 == debug_sub_mode)
        {
            // Voxel上面図X-Ray表示.
            const int3 bv_full_reso = cb_srvs.bbv.grid_resolution * k_bbv_per_voxel_resolution;
            const float visualize_scale = 0.5;
            float3 read_pos_world_base = (float3(dtid.x, 0.0, cb_srvs.tex_main_view_depth_size.y-1 - dtid.y) + 0.5) * visualize_scale * cb_srvs.bbv.cell_size/k_bbv_per_voxel_resolution;
            read_pos_world_base += cb_srvs.bbv.grid_min_pos;

            float write_data = 0.0;
            for(int yi = 0; yi < bv_full_reso.y; ++yi)
            {
                const float3 read_pos_world = read_pos_world_base + float3(0.0, yi, 0.0) * (cb_srvs.bbv.cell_size/k_bbv_per_voxel_resolution);

                const uint bit_value = read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_srvs.bbv.grid_resolution, cb_srvs.bbv.grid_toroidal_offset, cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size_inv, read_pos_world);

                float occupancy = float(bit_value);
                occupancy /= (float)bv_full_reso.y;

                write_data += occupancy * 8.0;
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0);
        }
        else if(7 == debug_sub_mode)
        {
            // HiBrick トレーサによる Voxel 単位 Trace のテスト.
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float fog_rate0 = pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2);
                const float fog_rate1 = saturate((curr_ray_t_ws.x - 70.0)/500.0);

                const float3 bbv_cell_id = floor((view_origin + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_srvs.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                debug_color.xyz = float4(noise_float_to_float(bbv_cell_id.xyzz), noise_float_to_float(bbv_cell_id.xzyy), noise_float_to_float(bbv_cell_id.xyzx), 1);

                // HiBrick 経由でのヒット確認モードとして少しだけ緑寄りにする.
                debug_color.xyz = lerp(debug_color.xyz, float3(0.2, 1.0, 0.2), 0.15);
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
            RWTexWork[dtid.xy] = debug_color;
        }
        else if(8 == debug_sub_mode)
        {
            // empty HiBrick skip count. 0 以外なら HiBrick accelerator が実際に効いている。
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            const float skip_rate = debug_count_to_rate(debug_ray_info.x);
            RWTexWork[dtid.xy] = float4(lerp(float3(0.02, 0.02, 0.02), float3(0.1, 1.0, 0.2), skip_rate), 1.0);
        }
        else if(9 == debug_sub_mode)
        {
            // occupied HiBrick descend count. HiBrick を通って Brick 走査へ降りた回数。
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            const float descend_rate = debug_count_to_rate(debug_ray_info.y);
            RWTexWork[dtid.xy] = float4(lerp(float3(0.02, 0.02, 0.02), float3(1.0, 0.7, 0.1), descend_rate), 1.0);
        }
        else if(10 == debug_sub_mode)
        {
            // Brick coarse check count.
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            const float brick_check_rate = debug_count_to_rate(debug_ray_info.z);
            RWTexWork[dtid.xy] = float4(lerp(float3(0.02, 0.02, 0.02), float3(0.1, 0.7, 1.0), brick_check_rate), 1.0);
        }
        else if(11 == debug_sub_mode)
        {
            // fine voxel / bitmask check count.
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            const float bitmask_check_rate = debug_count_to_rate(debug_ray_info.w);
            RWTexWork[dtid.xy] = float4(lerp(float3(0.02, 0.02, 0.02), float3(1.0, 0.2, 0.8), bitmask_check_rate), 1.0);
        }
        else if(12 == debug_sub_mode)
        {
            // skip efficiency = empty_skip / (empty_skip + occupied_descend).
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            trace_bbv_dev_hibrick(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            const float skip_efficiency = debug_ray_info.x / max(debug_ray_info.x + debug_ray_info.y, 1.0);
            RWTexWork[dtid.xy] = float4(lerp(float3(0.8, 0.1, 0.1), float3(0.1, 1.0, 0.2), skip_efficiency), 1.0);
        }
        else if(13 == debug_sub_mode)
        {
            // HiBrick トレーサ比較用: skip 無効版.
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev_hibrick_no_skip(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_srvs.bbv.grid_min_pos, cb_srvs.bbv.cell_size, cb_srvs.bbv.grid_resolution,
                cb_srvs.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float fog_rate0 = pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2);
                const float fog_rate1 = saturate((curr_ray_t_ws.x - 70.0)/500.0);

                const float3 bbv_cell_id = floor((view_origin + ray_dir_ws*(curr_ray_t_ws.x + 0.001)) * (cb_srvs.bbv.cell_size_inv*float(k_bbv_per_voxel_resolution)));
                debug_color.xyz = float4(noise_float_to_float(bbv_cell_id.xyzz), noise_float_to_float(bbv_cell_id.xzyy), noise_float_to_float(bbv_cell_id.xyzx), 1);

                // skip 無効版は赤寄りに着色して比較しやすくする.
                debug_color.xyz = lerp(debug_color.xyz, float3(1.0, 0.2, 0.2), 0.2);
                debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
            }
            RWTexWork[dtid.xy] = debug_color;
        }
    }
    // Category 1: WCP.
    else if(1 == debug_category)
    {
        if(0 == debug_sub_mode)
        {
            // Probe Atlas Textureの表示.
            const int2 texel_pos = dtid.xy * 0.1;
            if(any(cb_srvs.wcp.flatten_2d_width * k_wcp_probe_octmap_width_with_border <= texel_pos))
                return;

            const float4 probe_data = WcpProbeAtlasTex.Load(uint3(texel_pos, 0));
            RWTexWork[dtid.xy] = probe_data.xxxx;
        }
    }
    // Category 2: SSP_Oct.
    else if(2 == debug_category)
    {
        if(0 == debug_sub_mode)
        {
            // Screen Space Probe Atlas Textureの表示.
            const float4 probe_data = ScreenSpaceProbeTex.Load(uint3(texel_pos, 0));
            RWTexWork[dtid.xy] = probe_data;
        }
        else if(1 == debug_sub_mode)
        {
            // Screen Space Probe の Normalデバッグ.
            RWTexWork[dtid.xy] = float4(ss_probe_tile_normal_ws * 0.5 + 0.5, 1.0);
        }
        else if(2 == debug_sub_mode)
        {
            // Screen Space Probe の Tile内配置Positionデバッグ.
            if(isValidDepth(ss_probe_depth) && all(ss_probe_screen_tile_base_pos + ss_probe_pos_in_tile == texel_pos))
            {
                const float debug_d = 0.01 / (ss_probe_depth + 1e-6);
                RWTexWork[dtid.xy] = float4(cos(debug_d) * 0.5 + 0.5, cos(debug_d * 0.5)*0.5+0.5, cos(debug_d * 0.25)*0.5+0.5, 1.0);
            }
            else
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
        }
        else if(3 == debug_sub_mode)
        {
            // Screen Space ProbeのSkyVisibility投影デバッグ.
            const float3 sample_dir = -cb_srvs.main_light_dir_ws;
        
            // Octmapのテクセルが外側をBilinearSamplingしないようにクランプ.
            float2 octmap_texel_pos = clamp(SspEncodeDirByNormal(sample_dir, ss_probe_tile_normal_ws) * SCREEN_SPACE_PROBE_OCT_RESOLUTION, 0.5, SCREEN_SPACE_PROBE_OCT_RESOLUTION-0.5);
            float2 ss_probe_sample_texel_pos = ss_probe_atlas_tile_base_pos + octmap_texel_pos;

            // BilinearSampling. 半球ではないOctahedralMapであるためボーダー部の処理スキップによるカクツキが目立つ..
            float4 ss_probe_value = ScreenSpaceProbeTex.SampleLevel(SmpLinearClamp, ss_probe_sample_texel_pos / screen_size_f, 0);
            RWTexWork[dtid.xy] = ss_probe_value.xxxx;
        }
        else if(4 == debug_sub_mode)
        {
            // SH係数をそのままRGBAで可視化 (Y00=R, Y1_{-1}(y)=G, Y1_0(z)=B, Y1_{+1}(x)=A).
            const float4 ss_probe_sh = ScreenSpaceProbeSHTex.Load(int3(ss_probe_tile_id, 0));
            RWTexWork[dtid.xy] = ss_probe_sh;
        }
        else if(5 == debug_sub_mode)
        {
            // main_light_dir_ws方向のSH再評価結果を表示.
            const float3 sample_dir = normalize(-cb_srvs.main_light_dir_ws);
            const float4 sh_basis = EvaluateL1ShBasis(sample_dir);
            const float4 ss_probe_sh = ScreenSpaceProbeSHTex.Load(int3(ss_probe_tile_id, 0));
            const float sh_sample = max(0.0, dot(ss_probe_sh, sh_basis));
            RWTexWork[dtid.xy] = sh_sample.xxxx;
        }
        else if(6 == debug_sub_mode)
        {
            // Screen Space Probe の Reprojection成功可視化.
            if(!isValidDepth(ss_probe_depth))
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                const float3 debug_color = ss_probe_reprojection_succeeded ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
                RWTexWork[dtid.xy] = float4(debug_color, 1.0);
            }
        }
        else if(7 == debug_sub_mode)
        {
            // Screen Space Probe SideCache の生存状態可視化.
            if(0 == cb_srvs.ss_probe_side_cache_enable)
            {
                // SideCache無効.
                RWTexWork[dtid.xy] = float4(0.15, 0.15, 0.15, 1.0);
            }
            else
            {
                const float4 side_cache_meta = ScreenSpaceProbeSideCacheMetaTex.Load(int3(ss_probe_tile_id, 0));
                const float cached_frame_index = side_cache_meta.w;

                if(cached_frame_index < 0.5)
                {
                    // 未初期化キャッシュ.
                    RWTexWork[dtid.xy] = float4(0.0, 0.25, 1.0, 1.0);
                }
                else
                {
                    const float cache_age = max(0.0, float(cb_srvs.frame_count) - cached_frame_index);
                    const float max_life = max(1.0, float(cb_srvs.ss_probe_side_cache_max_life_frame));
                    if(cache_age > max_life)
                    {
                        // 期限切れキャッシュ.
                        RWTexWork[dtid.xy] = float4(0.8, 0.1, 0.9, 1.0);
                    }
                    else
                    {
                        // 生存キャッシュ: 新鮮=緑, 寿命末期=赤.
                        const float life_rate = saturate(cache_age / max_life);
                        const float3 debug_color = lerp(float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), life_rate);
                        RWTexWork[dtid.xy] = float4(debug_color, 1.0);
                    }
                }
            }
        }
    }
    // Category 3: SSP_SH (DirectSH).
    else if(3 == debug_category)
    {
        if(0 == debug_sub_mode)
        {
            // [DirectSH] Screen Space Probe の Normalデバッグ.
            const float4 dsh_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(ss_probe_tile_id, 0));
            if(!isValidDepth(dsh_tile_info.x))
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                const float3 dsh_normal_ws = OctDecode(dsh_tile_info.zw);
                RWTexWork[dtid.xy] = float4(dsh_normal_ws * 0.5 + 0.5, 1.0);
            }
        }
        else if(1 == debug_sub_mode)
        {
            // [DirectSH] Screen Space Probe の Tile内配置Positionデバッグ.
            const float4 dsh_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(ss_probe_tile_id, 0));
            const float dsh_depth = dsh_tile_info.x;
            const int2 dsh_pos_in_tile = SspTileInfoDecodeProbePosInTile(dsh_tile_info.y);
            if(isValidDepth(dsh_depth) && all(ss_probe_screen_tile_base_pos + dsh_pos_in_tile == texel_pos))
            {
                const float debug_d = 0.01 / (dsh_depth + 1e-6);
                RWTexWork[dtid.xy] = float4(cos(debug_d) * 0.5 + 0.5, cos(debug_d * 0.5)*0.5+0.5, cos(debug_d * 0.25)*0.5+0.5, 1.0);
            }
            else
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
        }
        else if(2 == debug_sub_mode)
        {
            // [DirectSH] SH係数をそのままRGBAで可視化 (Y00=R, Y1_{-1}(y)=G, Y1_0(z)=B, Y1_{+1}(x)=A).
            const float4 sh_coeff = ScreenSpaceProbeDirectSHTex.Load(int3(ss_probe_tile_id, 0));
            RWTexWork[dtid.xy] = sh_coeff;
        }
        else if(3 == debug_sub_mode)
        {
            // [DirectSH] main_light方向で DirectSH を評価した結果を可視化.
            const float4 dsh_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(ss_probe_tile_id, 0));
            if(!isValidDepth(dsh_tile_info.x))
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                const float3 sample_dir = normalize(-cb_srvs.main_light_dir_ws);
                const float4 sh_basis   = EvaluateL1ShBasis(sample_dir);
                const float4 sh_coeff   = ScreenSpaceProbeDirectSHTex.Load(int3(ss_probe_tile_id, 0));
                const float  sky_vis    = max(0.0, dot(sh_coeff, sh_basis));
                RWTexWork[dtid.xy] = sky_vis.xxxx;
            }
        }
        else if(4 == debug_sub_mode)
        {
            // [DirectSH] Reprojection成功/失敗可視化. 緑=成功, 赤=失敗.
            const float4 dsh_tile_info = ScreenSpaceProbeDirectSHTileInfoTex.Load(int3(ss_probe_tile_id, 0));
            if(!isValidDepth(dsh_tile_info.x))
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                const bool dsh_reprojection_succeeded = SspTileInfoIsReprojectionSucceeded(dsh_tile_info.y);
                const float3 debug_color = dsh_reprojection_succeeded ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
                RWTexWork[dtid.xy] = float4(debug_color, 1.0);
            }
        }
    }
}

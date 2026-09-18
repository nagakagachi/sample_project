
#include "../instant_rdv_util.hlsli"
#include "../assp/assp_probe_common.hlsli"

// SceneView定数バッファ構造定義.
#include "../../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;
Texture2D<float> TexHardwareDepth;
Texture2D<float4> TexReducedSurfaceBuffer;
SamplerState SmpReducedSurfaceBuffer;

RWTexture2D<float4>	RWTexWork;

bool bbv_debug_depth_test(float3 hit_pos_ws, int2 texel_pos)
{
    if(0 == cb_instant_rdv.debug_bbv_depth_test_enable)
    {
        return true;
    }

    const float3 hit_pos_vs = mul(
        cb_ngl_sceneview.cb_view_mtx,
        float4(hit_pos_ws, 1.0));
    const float4 hit_pos_cs = mul(
        cb_ngl_sceneview.cb_proj_mtx,
        float4(hit_pos_vs, 1.0));
    if(abs(hit_pos_cs.w) <= 1e-6)
    {
        return false;
    }

    const float hit_ndc_z = hit_pos_cs.z / hit_pos_cs.w;
    if(hit_ndc_z < 0.0 || hit_ndc_z > 1.0)
    {
        return false;
    }

    const float scene_depth = TexHardwareDepth.Load(int3(texel_pos, 0)).r;
    if(!isValidDepth(scene_depth))
    {
        return true;
    }

    const float hit_view_z = mul(
        cb_ngl_sceneview.cb_view_mtx,
        float4(hit_pos_ws, 1.0)).z;
    const float scene_view_z = calc_view_z_from_ndc_z(
        scene_depth,
        cb_ngl_sceneview.cb_ndc_z_to_view_z_coef);
    // MainViewは左手系なので、View Zが小さい方がカメラに近い。
    return hit_view_z <= scene_view_z + 1e-3;
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
    uint2 work_tex_size_u;
    RWTexWork.GetDimensions(work_tex_size_u.x, work_tex_size_u.y);
	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 work_tex_size_f = float2(work_tex_size_u);
	const float2 screen_size_f = float2(cb_instant_rdv.tex_main_view_depth_size.xy);
	const float2 screen_uv = (screen_pos_f / work_tex_size_f);
    const int2 texel_pos = clamp(int2(screen_uv * screen_size_f), int2(0, 0), int2(cb_instant_rdv.tex_main_view_depth_size.xy) - 1);
    
	const float3 view_origin = GetViewOriginFromInverseViewMatrix(cb_ngl_sceneview.cb_view_inv_mtx);
    
    const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, cb_ngl_sceneview.cb_proj_mtx);
    const float3 ray_dir_ws = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));

    const int debug_category = cb_instant_rdv.debug_view_category;
    const int debug_sub_mode = cb_instant_rdv.debug_view_sub_mode;

    // Category 0: BBV.
    if(0 == debug_category)
    {
        if(6 == debug_sub_mode || 7 == debug_sub_mode)
        {
            if(0 == cb_instant_rdv.main_view_reduced_surface_enable)
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
                return;
            }

            const float4 surface_sample =
                TexReducedSurfaceBuffer.SampleLevel(
                    SmpReducedSurfaceBuffer,
                    screen_uv,
                    0.0);
            if(surface_sample.x <= 0.0)
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
                return;
            }

            if(6 == debug_sub_mode)
            {
                const float3 normal_ws = normalize(
                    OctDecode(surface_sample.yz));
                RWTexWork[dtid.xy] = float4(
                    normal_ws * 0.5 + 0.5,
                    1.0);
            }
            else
            {
                const float confidence = saturate(surface_sample.w);
                RWTexWork[dtid.xy] = float4(
                    confidence.xxx,
                    1.0);
            }
            return;
        }

        if((0 == debug_sub_mode) || (1 == debug_sub_mode) || (3 == debug_sub_mode))
        {
            // Voxel単位Traceのテスト.
            const float trace_distance = 10000.0;          
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance, 
                cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size, cb_instant_rdv.bbv.grid_resolution,
                cb_instant_rdv.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float3 hit_pos_ws =
                    view_origin + ray_dir_ws * curr_ray_t_ws.x;
                const bool depth_test_pass = bbv_debug_depth_test(hit_pos_ws, texel_pos);
                if(!depth_test_pass)
                {
                    debug_color = float4(0.01, 0.01, 0.01, 1.0);
                }
                else
                {
                    const float fog_rate0 = pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2);
                    const float fog_rate1 = saturate((curr_ray_t_ws.x - 70.0)/500.0);
                    const float3 color_sample_pos_ws =
                        view_origin + ray_dir_ws * (curr_ray_t_ws.x + 0.001);

                    // デバッグ用テクスチャにモード別描画.
                    if(0 == debug_sub_mode)
                    {
                        // World-space FineVoxel ID color. Storage/Toroidal座標は使用しない.
                        const float3 fine_voxel_id = floor(
                            color_sample_pos_ws *
                            (cb_instant_rdv.bbv.cell_size_inv * float(k_bbv_per_voxel_resolution)));
                        debug_color.xyz = float3(
                            noise_float_to_float(fine_voxel_id.xyzz),
                            noise_float_to_float(fine_voxel_id.xzyy),
                            noise_float_to_float(fine_voxel_id.xyzx));
                    }
                    else if(1 == debug_sub_mode)
                    {
                        // Storage/Toroidal Brick ID color.
                        debug_color.xyz = float3(
                            noise_float_to_float(hit_voxel_index),
                            noise_float_to_float(hit_voxel_index * 2),
                            noise_float_to_float(hit_voxel_index * 3));

                        // 簡易フォグ.
                        debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), fog_rate0 * 0.8);
                        debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), fog_rate1 * 0.8);
                    }
                    else if(3 == debug_sub_mode)
                    {
                        // Bbvセルの深度を可視化.
                        debug_color.xyz = float3(saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0), saturate(curr_ray_t_ws.x/100.0));
                    }
                }
            }
            RWTexWork[dtid.xy] = debug_color;
        }
        else if(4 == debug_sub_mode)
        {
            // Brick単位Traceのテスト. Brickの占有フラグが適切に設定または除去されているかのテスト.
            const float trace_distance = 10000.0;          
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance, 
                cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size, cb_instant_rdv.bbv.grid_resolution,
                cb_instant_rdv.bbv.grid_toroidal_offset, BitmaskBrickVoxel, true);
                
            float4 debug_color = float4(0, 0, 1, 0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float3 hit_pos_ws =
                    view_origin + ray_dir_ws * curr_ray_t_ws.x;
                if(!bbv_debug_depth_test(hit_pos_ws, texel_pos))
                {
                    debug_color = float4(0.01, 0.01, 0.01, 1.0);
                }
                else
                {
                    // Storage/Toroidal Brick IDを可視化.
                    debug_color.xyz = float3(
                        noise_float_to_float(hit_voxel_index),
                        noise_float_to_float(hit_voxel_index * 2),
                        noise_float_to_float(hit_voxel_index * 3));

                    // 簡易フォグ.
                    debug_color.xyz = lerp(debug_color.xyz, float3(1,1,1), pow(saturate((curr_ray_t_ws.x - 20.0)/100.0), 1.0/1.2) * 0.8);
                    debug_color.xyz = lerp(debug_color.xyz, float3(0.1,0.1,1), saturate((curr_ray_t_ws.x - 70.0)/500.0) * 0.8);
                }
            }
            RWTexWork[dtid.xy] = debug_color;
        }
        else if(5 == debug_sub_mode)
        {
            // Voxel上面図X-Ray表示.
            const int3 bv_full_reso = cb_instant_rdv.bbv.grid_resolution * k_bbv_per_voxel_resolution;
            const float visualize_scale = 0.5;
            float3 read_pos_world_base = (float3(dtid.x, 0.0, cb_instant_rdv.tex_main_view_depth_size.y-1 - dtid.y) + 0.5) * visualize_scale * cb_instant_rdv.bbv.cell_size/k_bbv_per_voxel_resolution;
            read_pos_world_base += cb_instant_rdv.bbv.grid_min_pos;

            float write_data = 0.0;
            for(int yi = 0; yi < bv_full_reso.y; ++yi)
            {
                const float3 read_pos_world = read_pos_world_base + float3(0.0, yi, 0.0) * (cb_instant_rdv.bbv.cell_size/k_bbv_per_voxel_resolution);

                const uint bit_value = read_bbv_voxel_from_world_pos(BitmaskBrickVoxel, cb_instant_rdv.bbv.grid_resolution, cb_instant_rdv.bbv.grid_toroidal_offset, cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size_inv, read_pos_world);

                float occupancy = float(bit_value);
                occupancy /= (float)bv_full_reso.y;

                write_data += occupancy * 8.0;
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0);
        }
        else if(2 == debug_sub_mode)
        {
            // FineVoxel hit is colored with its containing Brick radiance.
            const float trace_distance = 10000.0;
            int hit_voxel_index = -1;
            float4 debug_ray_info;
            float4 curr_ray_t_ws = trace_bbv_dev(
                hit_voxel_index, debug_ray_info,
                view_origin, ray_dir_ws, trace_distance,
                cb_instant_rdv.bbv.grid_min_pos, cb_instant_rdv.bbv.cell_size, cb_instant_rdv.bbv.grid_resolution,
                cb_instant_rdv.bbv.grid_toroidal_offset, BitmaskBrickVoxel, false);

            float3 debug_color = float3(0.0, 0.0, 0.0);
            if(0.0 <= curr_ray_t_ws.x)
            {
                const float3 hit_pos_ws =
                    view_origin + ray_dir_ws * curr_ray_t_ws.x;
                if(!bbv_debug_depth_test(hit_pos_ws, texel_pos))
                {
                    debug_color = float3(0.01, 0.01, 0.01);
                }
                else
                {
                    const BbvOptionalData voxel_optional_data = BitmaskBrickVoxelOptionData[hit_voxel_index];
                    debug_color = voxel_optional_data.resolved_radiance / (1.0 + voxel_optional_data.resolved_radiance);
                    debug_color = pow(max(debug_color, 0.0.xxx), 1.0 / 2.2);
                }
            }
            RWTexWork[dtid.xy] = float4(debug_color, 1.0);
        }
    }
    // Category 1: FSP.
    else if(1 == debug_category)
    {
        if(0 == debug_sub_mode)
        {
            // FSP OctahedralMap atlas raw RGBA.
            const int2 texel_pos = dtid.xy * 0.1;
            uint tex_width, tex_height;
            FspProbeAtlasTex.GetDimensions(tex_width, tex_height);
            if(any(int2(tex_width, tex_height) <= texel_pos))
                return;

            RWTexWork[dtid.xy] = FspProbeAtlasTex.Load(uint3(texel_pos, 0));
        }
        else if(1 <= debug_sub_mode && debug_sub_mode <= 5)
        {
            // 各Cascadeを縦方向の1行とし、物理Zスライスを横方向へ並べる。
            // 表示座標を拡大率で戻してからスクロールを加え、仮想キャンバス上のセルを求める。
            const uint3 grid_resolution = uint3(cb_instant_rdv.fsp_cascade[0].grid.grid_resolution);
            const uint2 tile_stride = grid_resolution.xy + 1u;
            const uint display_scale = uint(max(
                cb_instant_rdv.debug_fsp_irradiance_volume_slice_scale,
                1));
            const uint2 scroll_offset = uint2(
                max(cb_instant_rdv.debug_fsp_irradiance_volume_slice_scroll_x, 0),
                max(cb_instant_rdv.debug_fsp_irradiance_volume_slice_scroll_y, 0));
            const uint2 virtual_coord = dtid.xy / display_scale + scroll_offset;
            const uint slice_z = virtual_coord.x / tile_stride.x;
            const uint cascade_index = virtual_coord.y / tile_stride.y;
            const uint2 physical_coord_xy = virtual_coord % tile_stride;
            if(any(physical_coord_xy >= grid_resolution.xy) ||
                slice_z >= grid_resolution.z ||
                cascade_index >= uint(cb_instant_rdv.fsp_cascade_count))
            {
                RWTexWork[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
                return;
            }

            const FspCascadeGridParam cascade = FspGetCascadeParam(cascade_index);
            const uint irradiance_volume_cell_index = cascade.cell_offset +
                FspPhysicalCellCoordToLocalIndex(
                    int3(physical_coord_xy, slice_z),
                    cascade.grid.grid_resolution);

            if(1 == debug_sub_mode)
            {
                // 旧Buffer表示との比較用に、各信号のL0係数をRGBAへ再構成する。
                float4 sky_visibility;
                float4 irradiance_r;
                float4 irradiance_g;
                float4 irradiance_b;
                FspIrradianceVolumeLoadSignals(
                    irradiance_volume_cell_index,
                    sky_visibility,
                    irradiance_r,
                    irradiance_g,
                    irradiance_b);
                RWTexWork[dtid.xy] = float4(
                    sky_visibility.x,
                    irradiance_r.x,
                    irradiance_g.x,
                    irradiance_b.x);
            }
            else
            {
                // 新3D Textureの信号サブボリュームを格納RGBAのまま表示する。
                RWTexWork[dtid.xy] = FspIrradianceVolumeLoadSignal(
                    irradiance_volume_cell_index,
                    uint(debug_sub_mode - 2));
            }
        }
    }
    // Category 2: ASSP.
    else if(2 == debug_category)
    {
        const int2 representative_tile_id = texel_pos / ADAPTIVE_SCREEN_SPACE_PROBE_INFO_DOWNSCALE;
        uint2 tile_info_size_u32;
        AdaptiveScreenSpaceProbeTileInfoTex.GetDimensions(tile_info_size_u32.x, tile_info_size_u32.y);
        const bool is_in_range = all(representative_tile_id >= int2(0, 0)) && all(representative_tile_id < int2(tile_info_size_u32));
        const float4 representative_tile_info = is_in_range ? AdaptiveScreenSpaceProbeTileInfoTex.Load(int3(representative_tile_id, 0)) : float4(1.0, 0.0, 0.0, 0.0);
        const bool is_valid_rep = is_in_range && isValidDepth(representative_tile_info.x);

        if(!is_valid_rep)
        {
            RWTexWork[dtid.xy] = float4(0.02, 0.02, 0.02, 1.0);
        }
        else
        {
            if(0 == debug_sub_mode)
            {
                RWTexWork[dtid.xy] = AdaptiveScreenSpaceProbeTex.Load(int3(texel_pos, 0));
            }
            else if(1 == debug_sub_mode)
            {
                uint2 packed_sh_tex_size;
                AdaptiveScreenSpaceProbePackedSHTex.GetDimensions(packed_sh_tex_size.x, packed_sh_tex_size.y);
                const int2 packed_sh_texel_pos = texel_pos / 2;
                if(any(packed_sh_texel_pos >= int2(packed_sh_tex_size)))
                {
                    RWTexWork[dtid.xy] = float4(0.02, 0.02, 0.02, 1.0);
                }
                else
                {
                    RWTexWork[dtid.xy] = AdaptiveScreenSpaceProbePackedSHTex.Load(int3(packed_sh_texel_pos, 0));
                }
            }
            else if(2 == debug_sub_mode)
            {
                const float4 sh_basis = EvaluateL1ShBasis(normalize(-cb_instant_rdv.main_light_dir_ws));
                const float4 coeff0 = AsspPackedShAtlasLoadCoeff(representative_tile_id, 0);
                const float4 coeff1 = AsspPackedShAtlasLoadCoeff(representative_tile_id, 1);
                const float4 coeff2 = AsspPackedShAtlasLoadCoeff(representative_tile_id, 2);
                const float4 coeff3 = AsspPackedShAtlasLoadCoeff(representative_tile_id, 3);
                const float3 radiance = max(float3(
                    dot(float4(coeff0.g, coeff1.g, coeff2.g, coeff3.g), sh_basis),
                    dot(float4(coeff0.b, coeff1.b, coeff2.b, coeff3.b), sh_basis),
                    dot(float4(coeff0.a, coeff1.a, coeff2.a, coeff3.a), sh_basis)), 0.0.xxx);
                RWTexWork[dtid.xy] = float4(radiance / (1.0 + radiance), 1.0);
            }
            else if(3 == debug_sub_mode)
            {
                const float4 variance_signal = AdaptiveScreenSpaceProbeVarianceTex.Load(int3(representative_tile_id, 0));
                const float filtered_mean = max(variance_signal.x, 0.0);
                const float mean_vis = filtered_mean / (1.0 + filtered_mean);
                RWTexWork[dtid.xy] = float4(mean_vis, mean_vis, mean_vis, 1.0);
            }
            else if(4 == debug_sub_mode)
            {
                const float4 variance_signal = AdaptiveScreenSpaceProbeVarianceTex.Load(int3(representative_tile_id, 0));
                const float filtered_second_moment = max(variance_signal.y, 0.0);
                const float filtered_mean = max(variance_signal.x, 0.0);
                const float filtered_variance = max(filtered_second_moment - filtered_mean * filtered_mean, 0.0);
                const float variance_vis = filtered_variance / (0.1 + filtered_variance);
                const float3 debug_color = lerp(float3(0.02, 0.02, 0.05), float3(1.0, 0.35, 0.1), variance_vis);
                RWTexWork[dtid.xy] = float4(debug_color, 1.0);
            }
            else if(5 == debug_sub_mode)
            {
                const float4 variance_signal = AdaptiveScreenSpaceProbeVarianceTex.Load(int3(representative_tile_id, 0));
                const float raw_mean = max(variance_signal.z, 0.0);
                const float mean_vis = raw_mean / (1.0 + raw_mean);
                RWTexWork[dtid.xy] = float4(mean_vis, mean_vis, mean_vis, 1.0);
            }
            else if(6 == debug_sub_mode)
            {
                const float4 variance_signal = AdaptiveScreenSpaceProbeVarianceTex.Load(int3(representative_tile_id, 0));
                const float raw_variance = max(variance_signal.w, 0.0);
                const float variance_vis = raw_variance / (0.1 + raw_variance);
                const float3 debug_color = lerp(float3(0.02, 0.02, 0.05), float3(1.0, 0.35, 0.1), variance_vis);
                RWTexWork[dtid.xy] = float4(debug_color, 1.0);
            }
            else if(7 == debug_sub_mode)
            {
                uint probe_linear_index = 0u;
                if(!AsspTryGetProbeLinearIndexFromTileId(representative_tile_id, probe_linear_index))
                {
                    RWTexWork[dtid.xy] = float4(0.02, 0.02, 0.02, 1.0);
                }
                else
                {
                    const uint packed_meta = AsspProbeRayMetaBuffer[probe_linear_index];
                    const uint ray_count_u = AsspUnpackRayMetaCount(packed_meta);
                    const float t = saturate(float(ray_count_u) / float(k_assp_ray_count_max));
                    RWTexWork[dtid.xy] = float4(t, t, t, 1.0);
                }
            }
            else
            {
                RWTexWork[dtid.xy] = float4(0.02, 0.02, 0.02, 1.0);
            }
        }
    }
}


#if 0

ss_voxel_debug_visualize_cs.hlsl

デバッグ可視化.

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
ConstantBuffer<DispatchParam> cb_dispatch_param;

Buffer<uint>		BufferWork;
Buffer<uint>		OccupancyBitmaskVoxel;

RWTexture2D<float4>	RWTexWork;


// レイの始点終点セットアップ. 領域AABBの内部または表面から開始するため.
bool setup_trace_ray(out float3 out_ray_origin, out float3 out_ray_end, out float3 out_ray_dir_inv, float3 aabb_min, float3 aabb_max, float3 ray_origin, float3 ray_end)
{
    const float3 ray_d_c = ray_end - ray_origin;
    const float3 ray_dir_c = normalize(ray_d_c);
    const float ray_len_c = dot(ray_dir_c, ray_d_c);

    // Inv Dir.
    const float k_nearly_zero_threshold = 1e-7;
    const float k_float_max = 1e20;
    out_ray_dir_inv = select( k_nearly_zero_threshold > abs(ray_dir_c), float3(k_float_max, k_float_max, k_float_max), 1.0f / ray_dir_c);

    const float3 t_to_min = (aabb_min - ray_origin) * out_ray_dir_inv;
    const float3 t_to_max = (aabb_max - ray_origin) * out_ray_dir_inv;
    const float3 t_near_v3 = min(t_to_min, t_to_max);
    const float3 t_far_v3 = max(t_to_min, t_to_max);
    const float t_near = max(t_near_v3.x, max(t_near_v3.y, t_near_v3.z));
    const float t_far = min(t_far_v3.x, min(t_far_v3.y, t_far_v3.z));

    // GridBoxとの交点が存在しなければ早期終了.
    // t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
    if (0.0f > t_far || t_near >= t_far || ray_len_c < t_near)
    {
        return false;;
    }

    // Grid内にクランプしたトレース始点終点.
    // 以降はこの始点終点で処理.
    out_ray_origin = (0.0 > t_near) ? ray_origin : ray_origin + t_near * ray_dir_c;
    out_ray_end = (ray_len_c < t_far) ? ray_end : ray_origin + t_far * ray_dir_c;

    return true;
};


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
	const float2 screen_size_f = float2(cb_dispatch_param.TexHardwareDepthSize.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);
    
    #if 1
        // レイキャストで可視化.
        
	    const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	    const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;
        
        const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        const float3 ray_dir_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));

        const float trace_distance = 10000.0;
                
        const float3 grid_box_min = float3(0.0, 0.0, 0.0);
        const float3 grid_box_max = float3(cb_dispatch_param.BaseResolution);
        const int3 grid_box_cell_max = int3(cb_dispatch_param.BaseResolution - 1);

        const float3 ray_begin_c = (camera_pos - cb_dispatch_param.GridMinPos) * cb_dispatch_param.CellSizeInv;
        const float3 ray_end_c = ((camera_pos + ray_dir_ws*trace_distance) - cb_dispatch_param.GridMinPos) * cb_dispatch_param.CellSizeInv;


        float3 ray_p0_clamped_c;
        float3 ray_p1_clamped_c;
        float3 ray_dir_inv_c;
        // レイセットアップ.
        if(!setup_trace_ray(ray_p0_clamped_c, ray_p1_clamped_c, ray_dir_inv_c, grid_box_min, grid_box_max, ray_begin_c, ray_end_c))
        {
            RWTexWork[dtid.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
            return;
        }

        const float3 ray_d_c = ray_p1_clamped_c - ray_p0_clamped_c;
        const float3 ray_dir_c = normalize(ray_d_c);
        const float ray_d_c_len = dot(ray_dir_c, ray_d_c);
        const float ray_d_c_len_inv = 1.0 / ray_d_c_len;

        // Inv Dir.
        const float3 dir_sign = sign(ray_dir_c);// +1 : positive component or zero, -1 : negative component.
        const float3 cell_delta = dir_sign * ray_dir_inv_c;

        // 0ベースでのトラバースCell範囲.
        const int3 trace_begin_cell = min(floor(ray_p0_clamped_c), grid_box_cell_max);
        const int3 trace_end_cell = min(floor(ray_p1_clamped_c), grid_box_cell_max);
        const int3 trance_cell_range = abs(trace_end_cell - trace_begin_cell);

        // 始点からの最初のステップt.
        const float3 cell_delta_offset = abs((floor(ray_p0_clamped_c) + max(dir_sign, float3(0,0,0)) - ray_p0_clamped_c) * ray_dir_inv_c);

        float curr_ray_t = 1.0;//1e20;
        int3 total_cell_step = int3(0,0,0);
        int3 prev_cell_step = int3(0,0,0);
        float3 cell_delta_accum = float3(0,0,0);
        for (;;)
        {
            const float cur_cell_delta = min(cell_delta_accum.x, min(cell_delta_accum.y, cell_delta_accum.z));

            // 到達Cell
            const int3 trace_cell_id = trace_begin_cell + int3(dir_sign) * total_cell_step;
            // 読み取り用のマッピング.
            int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(trace_cell_id, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
            uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

            for(int i = 0; i < k_per_voxel_occupancy_u32_count; ++i)
            {
                const uint voxel_elem_bitmask = OccupancyBitmaskVoxel[voxel_addr * k_per_voxel_occupancy_u32_count + i];
                // 現状は内部ビットまでは確認せずに内部に非ゼロビットがひとつでもあればヒット扱い.
                if(0 == voxel_elem_bitmask)
                    continue;
                
                // ヒット判定処理パラメータ構築. このトレースではcur_cell_deltaが直接レイ上の位置になるため, レイの長さで正規化した値を渡す.
                const float ray_t = cur_cell_delta * ray_d_c_len_inv;
                // CellId範囲チェックとは別にt値のチェック. CellID範囲チェックだけでは広いルート階層換算での終了判定なので実際には線分の範囲外になっても継続してしまうため.
                if (1.0f > ray_t)
                {
                    if (curr_ray_t > ray_t)
                    {
                        // 最近接 t を更新.
                        curr_ray_t = ray_t;
                        // DDAアルゴリズムであるため早期終了で最近接.
                    }
                }
            }

            if(1.0 > curr_ray_t)
                break;// ヒットしたので終了.


            // 探査続行.
            {
                cell_delta_accum = cell_delta_offset + float3(total_cell_step) * cell_delta;

                // xyzで最小値コンポーネントを探す.
                prev_cell_step = select(cell_delta_accum <= min(float3(cell_delta_accum.y, cell_delta_accum.z, cell_delta_accum.x), float3(cell_delta_accum.z, cell_delta_accum.x, cell_delta_accum.y)), int3(1,1,1), int3(0,0,0));
                if (true)
                {
                    // 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先.). 
                    // この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
                    int tmp = prev_cell_step.x;
                    prev_cell_step.y = (0 < tmp) ? 0 : prev_cell_step.y;
                    tmp += prev_cell_step.y;
                    prev_cell_step.z = (0 < tmp) ? 0 : prev_cell_step.z;
                }
                // ステップは整数ベースで進める.
                total_cell_step += prev_cell_step;

                // 範囲チェックとbreak.
                if (trance_cell_range.x < total_cell_step.x || trance_cell_range.y < total_cell_step.y || trance_cell_range.z < total_cell_step.z)
                    break;
            }
        }

        RWTexWork[dtid.xy] = (1.0 > curr_ray_t) ? float4(curr_ray_t, curr_ray_t, curr_ray_t, 1) : float4(0, 0, 0, 0);

    #else
        // 上面でボクセル可視化.
        uint2 read_voxel_xz = dtid.xy / 8;// 1ボクセルを何ピクセルとして画面に出すか.
        // ビットマスクボクセルの解像度分描画する.
        const int3 bv_full_reso = cb_dispatch_param.BaseResolution * k_per_voxel_occupancy_reso;
        if(all(read_voxel_xz < bv_full_reso.xz))
        {
            float write_data = 0.0;
            for(int yi = 0; yi < bv_full_reso.y; ++yi)
            {
                const int3 bitmask_coord = int3(read_voxel_xz.x, yi, (bv_full_reso.z - 1) - read_voxel_xz.y);
                
                // bitmaskが格納されているボクセルを読み出し.
                const int3 voxel_coord = bitmask_coord / k_per_voxel_occupancy_reso;
                int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
                uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

                const int3 voxel_inner_coord = bitmask_coord - voxel_coord*k_per_voxel_occupancy_reso;
                
                uint bitmask_u32_offset;
                uint bitmask_u32_bit_pos;
                calc_occupancy_bitmask_voxel_inner_bit_info(bitmask_u32_offset, bitmask_u32_bit_pos, voxel_inner_coord);

                // 該当する位置のビットを取り出し.
                const uint voxel_elem_bitmask = OccupancyBitmaskVoxel[voxel_addr * k_per_voxel_occupancy_u32_count + bitmask_u32_offset];
                const uint occupancy_bit = (voxel_elem_bitmask >> bitmask_u32_bit_pos) & 0x1;

                float occupancy = float(occupancy_bit);
                occupancy /= (float)bv_full_reso.y;

                write_data += occupancy * 8.0;
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0f);
        }
    #endif
}
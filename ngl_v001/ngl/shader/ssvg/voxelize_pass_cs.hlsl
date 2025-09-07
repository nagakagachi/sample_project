
#if 0

ss_voxelize_cs.hlsl

ハードウェア深度バッファからリニア深度バッファを生成

#endif


#include "ssvg_util.hlsli"

// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> ngl_cb_sceneview;
ConstantBuffer<DispatchParam> cb_dispatch_param;

Texture2D			TexHardwareDepth;
SamplerState		SmpHardwareDepth;

RWBuffer<uint>		RWBufferWork;
RWBuffer<uint>		RWVoxelOccupancyBitmask;

#define TILE_WIDTH 16

// DepthBufferに対してDispatch.
[numthreads(TILE_WIDTH, TILE_WIDTH, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	const float3 camera_dir = normalize(ngl_cb_sceneview.cb_view_inv_mtx._m02_m12_m22);// InvShadowViewMtxから向きベクトルを取得.
	const float3 camera_pos = ngl_cb_sceneview.cb_view_inv_mtx._m03_m13_m23;

	const float2 screen_pos_f = float2(dtid.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(cb_dispatch_param.TexHardwareDepthSize.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

    float d = TexHardwareDepth.Load(int3(dtid.xy, 0)).r;
    float view_z = ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.x / (d * ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.y + ngl_cb_sceneview.cb_ndc_z_to_view_z_coef.z);

    // Skyチェック.
    if(65535.0 > abs(view_z))
    {
        // 深度->PixelWorldPosition
        const float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
        const float3 pixel_pos_ws = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4((to_pixel_ray_vs/abs(to_pixel_ray_vs.z)) * view_z, 1.0));

        // PixelWorldPosition->VoxelCoord
        const float3 voxel_coordf = (pixel_pos_ws - cb_dispatch_param.GridMinPos) * cb_dispatch_param.CellSizeInv;
        const int3 voxel_coord = floor(voxel_coordf);
        if(all(voxel_coord >= 0) && all(voxel_coord < cb_dispatch_param.BaseResolution))
        {
            int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
            uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

            // Voxelの占有数カウンタ(仮)
            uint origin_value;
            InterlockedAdd(RWBufferWork[voxel_addr], 1, origin_value);

            // 占有ビットマスク.
            const float3 voxel_coord_frac = saturate(voxel_coordf - voxel_coord);
            const uint3 voxel_coord_bitmask_pos = uint3(voxel_coord_frac * VoxelOccupancyBitmaskReso);
            const uint bitmask_pos_x = (voxel_coord_bitmask_pos.x&VoxelOccupancyBitmaskAxisMask);
            const uint bitmask_pos_y = (voxel_coord_bitmask_pos.y&VoxelOccupancyBitmaskAxisMask);
            const uint bitmask_pos_z = (voxel_coord_bitmask_pos.z&VoxelOccupancyBitmaskAxisMask);
            const uint bitmask_bit_pos = bitmask_pos_x + (bitmask_pos_y * VoxelOccupancyBitmaskReso) + (bitmask_pos_z * (VoxelOccupancyBitmaskReso*VoxelOccupancyBitmaskReso));
            const uint bitmask_u32_index = bitmask_bit_pos / 32;
            const uint bitmask_u32_bit_pos = bitmask_bit_pos - (bitmask_u32_index * 32);
            const uint bitmask_append = (1 << bitmask_u32_bit_pos);
            InterlockedOr(RWVoxelOccupancyBitmask[voxel_addr * PerVoxelOccupancyU32Count + bitmask_u32_index], bitmask_append);
        }
    }
}
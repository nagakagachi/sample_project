
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

RWTexture2D<float4>	RWTexWork;


// デバッグテクスチャに対してDispatch.
[numthreads(16, 16, 1)]
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

    uint2 read_voxel_xz = dtid.xy / 10;// 適当なサイズで可視化.
    if(all(read_voxel_xz < cb_dispatch_param.BaseResolution.xz))
    {
        float write_data = 0.0;
        for(int yi = 0; yi < cb_dispatch_param.BaseResolution.y; ++yi)
        {
            // VoxelBufferから高さ方向に操作して総和を計算(表示用にY(Z)反転)
            int3 voxel_coord = int3(read_voxel_xz.x, yi, (cb_dispatch_param.BaseResolution.z - 1) - read_voxel_xz.y);
            // Toroidalマッピング.
            int3 voxel_coord_toroidal = (voxel_coord + cb_dispatch_param.GridToroidalOffset) % cb_dispatch_param.BaseResolution;
            //int3 voxel_coord_toroidal = voxel_coord;// デバッグ用でToroidalマッピングなし.

            uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);
            uint voxel_value = BufferWork[voxel_addr];

            write_data += clamp(float(voxel_value) / 100.0, 0.0, 3.0) / (float)cb_dispatch_param.BaseResolution.y;
        }

        RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0f);
    }
}
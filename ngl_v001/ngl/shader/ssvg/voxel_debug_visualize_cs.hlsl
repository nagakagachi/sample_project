
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
Buffer<uint>		VoxelOccupancyBitmask;

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

    #if 0
        uint2 read_voxel_xz = dtid.xy / 16;// 1ボクセルを何ピクセルとして画面に出すか.
        // ボクセル単位デバッグ描画(ビットマスクボクセルであってもボクセル単位).
        if(all(read_voxel_xz < cb_dispatch_param.BaseResolution.xz))
        {
            float write_data = 0.0;

            for(int yi = 0; yi < cb_dispatch_param.BaseResolution.y; ++yi)
            {
                // VoxelBufferから高さ方向に操作して総和を計算(表示用にY(Z)反転)
                int3 voxel_coord = int3(read_voxel_xz.x, yi, (cb_dispatch_param.BaseResolution.z - 1) - read_voxel_xz.y);
                int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
                uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

                #if 0
                    uint voxel_value = BufferWork[voxel_addr];

                    write_data += clamp(float(voxel_value) / 100.0, 0.0, 3.0) / (float)cb_dispatch_param.BaseResolution.y;
                #elif 1
                    // 占有ビットマスクから密度計算.
                    float occupancy = 0.0;
                    for(int obi = 0; obi < PerVoxelOccupancyU32Count; ++obi)
                    {
                        uint bitmask_value = VoxelOccupancyBitmask[voxel_addr * PerVoxelOccupancyU32Count + obi];
                        // ビット数を数える.
                        occupancy += (float(countbits(bitmask_value)) / float(PerVoxelOccupancyBitCount-1));
                    }
                    occupancy /= (float)cb_dispatch_param.BaseResolution.y;

                    write_data += occupancy;
                #endif
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0f);
        }
    #else
        uint2 read_voxel_xz = dtid.xy / 4;// 1ボクセルを何ピクセルとして画面に出すか.
        // ビットマスクボクセルの解像度分描画する.
        const int3 bv_full_reso = cb_dispatch_param.BaseResolution * VoxelOccupancyBitmaskReso;
        if(all(read_voxel_xz < bv_full_reso.xz))
        {
            float write_data = 0.0;

            for(int yi = 0; yi < bv_full_reso.y; ++yi)
            {
                const int3 bitmask_coord = int3(read_voxel_xz.x, yi, (bv_full_reso.z - 1) - read_voxel_xz.y);
                
                // bitmaskが格納されているボクセルを読み出し.
                const int3 voxel_coord = bitmask_coord / VoxelOccupancyBitmaskReso;
                int3 voxel_coord_toroidal = voxel_coord_toroidal_mapping(voxel_coord, cb_dispatch_param.GridToroidalOffset, cb_dispatch_param.BaseResolution);
                uint voxel_addr = voxel_coord_to_addr(voxel_coord_toroidal, cb_dispatch_param.BaseResolution);

                const int3 voxel_inner_coord = bitmask_coord - voxel_coord*VoxelOccupancyBitmaskReso;
                
                uint bitmask_u32_offset;
                uint bitmask_u32_bit_pos;
                calc_bitmask_voxel_offset_and_bitlocation(bitmask_u32_offset, bitmask_u32_bit_pos, voxel_inner_coord);

                // 該当する位置のビットを取り出し.
                const uint voxel_elem_bitmask = VoxelOccupancyBitmask[voxel_addr * PerVoxelOccupancyU32Count + bitmask_u32_offset];
                const uint occupancy_bit = (voxel_elem_bitmask >> bitmask_u32_bit_pos) & 0x1;

                float occupancy = float(occupancy_bit);
                occupancy /= (float)bv_full_reso.y;

                write_data += occupancy * 10.0;
            }

            RWTexWork[dtid.xy] = float4(write_data, write_data, write_data, 1.0f);
        }
    #endif
}
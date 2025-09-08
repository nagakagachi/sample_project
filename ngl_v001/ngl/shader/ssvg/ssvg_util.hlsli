
#if 0

ss_voxelize_util.hlsli

#endif

#include "../include/math_util.hlsli"

// Cpp側と一致させる.
// Voxelの占有度合いをビットマスク近似する際の1軸の解像度. 2の冪でなくても良い.
#define VoxelOccupancyBitmaskReso (8)
#define PerVoxelOccupancyBitCount (VoxelOccupancyBitmaskReso*VoxelOccupancyBitmaskReso*VoxelOccupancyBitmaskReso)
#define PerVoxelOccupancyU32Count ((PerVoxelOccupancyBitCount + 31) / 32)
#define VoxelOccupancyBitmaskAxisMask ((1 << (VoxelOccupancyBitmaskReso + 1)) - 1)


struct DispatchParam
{
    int3 BaseResolution;
    uint Flag;

    float3 GridMinPos;
    float CellSize;
    int3 GridToroidalOffset;
    float CellSizeInv;

    int3 GridToroidalOffsetPrev;
    int Dummy0;
    
    int3 GridCellDelta;// Toroidalではなくワールド空間Cellでのフレーム移動量.
    int Dummy1;

    int2 TexHardwareDepthSize;
};


#define VOXEL_ADDR_MODE 0
// Coordからアドレス計算(リニア).
uint voxel_coord_to_addr_linear(int3 coord, int3 resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}
// アドレスからCoord計算(リニア).
int3 addr_to_voxel_coord_linear(uint addr, int3 resolution)
{
    int z = addr / (resolution.x * resolution.y);
    addr -= z * (resolution.x * resolution.y);
    int y = addr / resolution.x;
    addr -= y * resolution.x;
    int x = addr;
    return int3(x, y, z);
}

// Coordからアドレス計算.
uint voxel_coord_to_addr(int3 coord, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
    #endif
}
// アドレスからCoord計算.
int3 addr_to_voxel_coord(uint addr, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        int z = addr / (resolution.x * resolution.y);
        addr -= z * (resolution.x * resolution.y);
        int y = addr / resolution.x;
        addr -= y * resolution.x;
        int x = addr;
    #endif
    return int3(x, y, z);
}


// リニアなVoxel座標をループするToroidalマッピングに変換する.
int3 voxel_coord_toroidal_mapping(int3 voxel_coord, int3 toroidal_offset, int3 resolution)
{
    return (voxel_coord + toroidal_offset) % resolution;
}

// Bitmask Voxel内の座標を元にBit要素を読み取るための情報を計算.
void calc_bitmask_voxel_offset_and_bitlocation(out uint out_u32_offset, out uint out_bit_location, uint3 bit_position_in_voxel)
{
    const uint3 bit_pos = (bit_position_in_voxel);
    const uint bit_linear_pos = bit_pos.x + (bit_pos.y * VoxelOccupancyBitmaskReso) + (bit_pos.z * (VoxelOccupancyBitmaskReso * VoxelOccupancyBitmaskReso));
    out_u32_offset = bit_linear_pos / 32;
    out_bit_location = bit_linear_pos - (out_u32_offset * 32);
}


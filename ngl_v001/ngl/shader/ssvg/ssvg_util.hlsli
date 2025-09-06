
#if 0

ss_voxelize_util.hlsli

#endif


#include "../include/math_util.hlsli"

struct DispatchParam
{
    int3 BaseResolution;
    uint Flag;

    float3 GridMinPos;
    float CellSize;
    float3 GridMinPosPrev;
    float CellSizeInv;
    
    int3 GridTroidalOffset;
    int Dummy;

    int2 TexHardwareDepthSize;
};


#define VOXEL_ADDR_MODE 0
// Coordからアドレス計算(リニア).
uint voxel_coord_to_addr_linear(int3 coord, int3 resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}
// アドレスからCoord計算(リニア).
uint3 addr_to_voxel_coord_linear(uint addr, int3 resolution)
{
    uint z = addr / (resolution.x * resolution.y);
    addr -= z * (resolution.x * resolution.y);
    uint y = addr / resolution.x;
    addr -= y * resolution.x;
    uint x = addr;
    return uint3(x, y, z);
}

// Coordからアドレス計算.
uint voxel_coord_to_addr(int3 coord, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
    #endif
}
// アドレスからCoord計算.
uint3 addr_to_voxel_coord(uint addr, int3 resolution)
{
    #if 0 == VOXEL_ADDR_MODE
        uint z = addr / (resolution.x * resolution.y);
        addr -= z * (resolution.x * resolution.y);
        uint y = addr / resolution.x;
        addr -= y * resolution.x;
        uint x = addr;
    #endif
    return uint3(x, y, z);
}

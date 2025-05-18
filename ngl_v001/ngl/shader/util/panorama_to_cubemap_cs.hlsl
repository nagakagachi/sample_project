
#if 0
パノラマイメージからCubemapを生成するCompute

Dispatchは x,y はplaneの解像度依存, z はCubemapの6面に対応する6を指定する

#endif

#include "../include/math_util.hlsli"

Texture2D tex_panorama;
SamplerState samp;

RWTexture2DArray<float4> uav_cubemap_as_array;


[numthreads(16,16,1)]
void main(
    uint3 dtid    : SV_DispatchThreadID,
    uint3 gtid    : SV_GroupThreadID,
    uint3 gid     : SV_GroupID,
    uint  gindex  : SV_GroupIndex
)
{
    const uint plane_index = gid.z;
    const uint2 texel_pos = dtid.xy;

    float3 front, up, right;
    GetCubemapPlaneAxis(plane_index, front, up, right);

    float width, height, count;
    uav_cubemap_as_array.GetDimensions(width, height, count);
    const float2 cubemap_uv = (float2(texel_pos) + float2(0.5, 0.5)) / float2(width, height);

    const float2 clip_space_pos_xy = cubemap_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float3 sample_ray_dir = normalize( (up * clip_space_pos_xy.y) + (right * clip_space_pos_xy.x) + front);

    const float2 panorama_uv = CalcPanoramaTexcoordFromWorldSpaceRay(sample_ray_dir);
    
    float4 tex_color = tex_panorama.SampleLevel(samp, panorama_uv, 0);    

    // 書き込み.
    uav_cubemap_as_array[uint3(texel_pos, plane_index)] = tex_color;
}

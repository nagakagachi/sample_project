
#if 0
入力Cubemapを元にGGX Specular Irradiance Cubemapを生成する.
Dispatch毎にMip[i]の6 planeを生成するため, Roughness毎のMipそれぞれに対してDispatchをする.

Dispatchは x,y はplaneの解像度依存, z はCubemapの6面に対応する6を指定する

利用のためにはこのmapとペアとなるSplit Sum ApproximationのBRDF LUT を使うか, 近似計算を使うことになる.

参考
https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/
https://learnopengl.com/PBR/IBL/Specular-IBL

#endif

#include "../include/math_util.hlsli"


#define CONV_GGX_SPECULAR_SAMPLING_COUNT (1024*1)


struct CbConvCubemapGgxSpecular
{
	uint use_mip_to_prevent_undersampling;
    float roughness;
};
ConstantBuffer<CbConvCubemapGgxSpecular> cb_conv_cubemap_ggx_specular;

TextureCube tex_cube;
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


    float input_width, input_height;
    tex_cube.GetDimensions(input_width, input_height);

    float output_width, output_height, output_plane_count;
    uav_cubemap_as_array.GetDimensions(output_width, output_height, output_plane_count);
    const float2 cubemap_uv = (float2(texel_pos) + float2(0.5, 0.5)) / float2(output_width, output_height);

    const float2 clip_space_pos_xy = cubemap_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float3 sample_normal = normalize( (up * clip_space_pos_xy.y) + (right * clip_space_pos_xy.x) + front);
	
    const float3 sample_right = normalize(cross(float3(0.0, 1.0, 0.0), sample_normal));
    const float3 sample_up = normalize(cross(sample_normal, sample_right));


    const float3 N = sample_normal;    
    const float3 R = N;
    const float3 V = R;

    float4 tex_color = (float4)0;
    float total_weight = 0.0;
    // Importance Sampling GGX.
    for(int si = 0; si < CONV_GGX_SPECULAR_SAMPLING_COUNT; ++si)
    {
        const float2 xi = Hammersley2d(si, CONV_GGX_SPECULAR_SAMPLING_COUNT);
        
        const float3 H = ImportanceSampleGGX(xi, N, cb_conv_cubemap_ggx_specular.roughness);
        const float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            const float4 cubemap_color = tex_cube.SampleLevel(samp, L, 0);

            tex_color += cubemap_color * NdotL;// cos重み付け.
            total_weight += NdotL;// 重みトータル.
        }
    }
    tex_color /= total_weight;// 重みで正規化.

    // 書き込み.
    uav_cubemap_as_array[uint3(texel_pos, plane_index)] = tex_color;
}

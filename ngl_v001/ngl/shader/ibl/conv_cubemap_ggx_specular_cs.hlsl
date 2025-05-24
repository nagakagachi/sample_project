
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
#include "../include/brdf.hlsli"


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
    const float3 V = N;

    float4 tex_color = (float4)0;
    float total_weight = 0.0;
    // Importance Sampling GGX with SplitSumApprox.
    // https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
    for(int si = 0; si < CONV_GGX_SPECULAR_SAMPLING_COUNT; ++si)
    {
        const float2 xi = Hammersley2d(si, CONV_GGX_SPECULAR_SAMPLING_COUNT);
        
        const float3 H = ImportanceSampleHalfVectorGGX(xi, N, cb_conv_cubemap_ggx_specular.roughness);
        const float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            // サンプルしたHから計算したLについてpdfを計算し, サンプル密度と入力テクセル密度の比から輝度を取得するMipLevelを補正することでエイリアシング低減する.
            float sample_mip_bias = 0.0;
            if(0!=cb_conv_cubemap_ggx_specular.use_mip_to_prevent_undersampling)
            {
                const float ggx_d = brdf_trowbridge_reitz_D(cb_conv_cubemap_ggx_specular.roughness, N, V, L);
                // 確率密度として積分して1となるような補正としての dot(N,H).
                const float pdf_h = ggx_d * dot(N, H);

                // ggx_d*dot(N,H) はHalfVectorの分布であるため反射ベクトルの分布に変換するためにJacobianを適用.
                const float jacobian_wm_to_wo = 1.0 / (4.0 * dot(L, H));
                // 反射方向のpdf.
                const float pdf_wo = pdf_h * jacobian_wm_to_wo;

                const float input_texel_density  = 4.0 * NGL_PI / (6.0 * input_width * input_height);
                const float sample_density = 1.0 / (float(CONV_GGX_SPECULAR_SAMPLING_COUNT) * pdf_wo + 0.0001);

                const float constant_mip_bias = 0.5;// https://learnopengl.com/PBR/IBL/Specular-IBL
                sample_mip_bias = (cb_conv_cubemap_ggx_specular.roughness == 0.0) ? 0.0 : constant_mip_bias * log2(sample_density / input_texel_density);
            }

            const float4 cubemap_color = tex_cube.SampleLevel(samp, L, sample_mip_bias);

            // Split Sum Approx における近似GGXサンプリングの場合の誤差を改善するために cos重み付け平均が提案されている.
            tex_color += cubemap_color * NdotL;
            total_weight += NdotL;
        }
    }
    tex_color /= total_weight;// 重みで正規化.

    // 書き込み.
    uav_cubemap_as_array[uint3(texel_pos, plane_index)] = tex_color;
}

#if 0
Split Sum Approximation用GGX BRDF Look-up Textureを生成する.

X-axis: NoV (0.0 - 1.0)
Y-axis: Roughness (0.0 - 1.0)
output: 
    R: Scale
    G: Bias

参考
https://github.com/derkreature/IBLBaker/blob/master/data/shadersD3D11/smith.brdf
https://github.com/derkreature/IBLBaker/blob/master/data/shadersD3D11/IblBrdf.hlsl


https://learnopengl.com/PBR/IBL/Specular-IBL
https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
https://bruop.github.io/ibl/
https://github.com/SaschaWillems/Vulkan-glTF-PBR/blob/master/data/shaders/genbrdflut.frag
http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
https://github.com/derkreature/IBLBaker/blob/master/data/shadersD3D11/IblBrdf.hlsl
http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html

#endif


#include "../include/math_util.hlsli"
#include "../include/brdf.hlsli"

#define GGX_BRDF_LUT_SAMPLING_COUNT (1024*2)

RWTexture2D<float4> uav_brdf_lut;


//------------------------------------------------------------------------------------//
// LUT compute functions used by IblBrdf.hlsl                                         //
//------------------------------------------------------------------------------------//
// Geometry term
// http://graphicrants.blogspot.com.au/2013/08/specular-brdf-reference.html
// I could not have arrived at this without the notes at :
// http://www.gamedev.net/topic/658769-ue4-ibl-glsl/
//　GGX Geometry Term for Smith's method.
float GGX_G(float NoV, float roughness_pow2)
{
    float r2 = pow(roughness_pow2, 2);
    return NoV*2 / (NoV + sqrt((NoV*NoV) * (1.0f - r2) + r2));
}

// Fresnel Term.
// Inputs, view dot half angle.
float fresnelForLut(float VoH)
{
    return pow(1.0-VoH, 5);
}

// Summation of Lut term while iterating over samples
float2 sumLut(float2 current, float G, float V, float F, float VoH, float NoL, float NoH, float NoV)
{
    float G_Vis = (G * V) * VoH / (NoH * NoV);
    current.x += (1.0 - F) * G_Vis;
    current.y += F * G_Vis;

    return current;
}

float2 integrate_split_sum_approx_scale_bias(float roughness, float NoV)
{
    const float3 N = float3(0.0f, 0.0f, 1.0f);
    const float3 V = float3(sqrt(1.0f - NoV * NoV), 0.0f, NoV);
    const float roughness_pow2 = roughness*roughness;
    
    const precise float Vis = GGX_G(NoV, roughness_pow2);

    float2 result = float2(0,0);
    for (uint i = 0; i < GGX_BRDF_LUT_SAMPLING_COUNT; i++)
    {
        const float2 Xi = Hammersley2d(i, GGX_BRDF_LUT_SAMPLING_COUNT);
        const float3 H = ImportanceSampleHemisphereHalfVectorGGX(Xi, roughness);
        
        const precise float3 L = 2.0f * dot(V, H) * H - V;

        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        const float VoH = saturate(dot(V, H));
        const float NoV = saturate(dot(N, V));
        if (NoL > 0)
        {
            // DirectX等では roughness が0に近い領域で計算誤差が大きくなりライン上のアーティファクトが発生するため, precise 指定が必要. 
            const precise float G = GGX_G(NoL, roughness_pow2);
            const precise float F = fresnelForLut(VoH);
            result = sumLut(result, G, Vis, F, VoH, NoL, NoH, NoV); 
        }
    }

    result /= float(GGX_BRDF_LUT_SAMPLING_COUNT);
    return result;
}


[numthreads(16, 16, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    float output_width, output_height;
    uav_brdf_lut.GetDimensions(output_width, output_height);

    float roughness = (float)(id.y+0.5) / (float)output_height;
    float NoV = (float)(id.x+0.5)  / (float)output_width;
    
    float2 result = integrate_split_sum_approx_scale_bias(roughness, NoV); 

    ////uav_brdf_lut[int2(id.x, (output_width-1)-id.y)] = float4(result.x, result.y, roughness, 1);
    uav_brdf_lut[int2(id.x, id.y)] = float4(result.x, result.y, roughness, 0);
}

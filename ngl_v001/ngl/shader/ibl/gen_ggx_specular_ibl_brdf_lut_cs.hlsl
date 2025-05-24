#if 0
Split Sum Approximation用GGX BRDF Look-up Textureを生成する.

X-axis: NoV (0.0 - 1.0)
Y-axis: Roughness (0.0 - 1.0)
output: 
    R: Scale
    G: Bias
    B: unused
    A: unused

参考
https://learnopengl.com/PBR/IBL/Specular-IBL
https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
https://bruop.github.io/ibl/
https://github.com/SaschaWillems/Vulkan-glTF-PBR/blob/master/data/shaders/genbrdflut.frag
http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html

#endif

#include "../include/math_util.hlsli"
#include "../include/brdf.hlsli"

#define GGX_BRDF_LUT_SAMPLING_COUNT (1024*2)

RWTexture2D<float4> uav_brdf_lut;



float GDFG(float NoV, float NoL, float roughness) {
    float a2 = roughness * roughness;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return (2 * NoL) / (GGXV + GGXL);
}

[numthreads(16,16,1)]
void main(
    uint3 dtid    : SV_DispatchThreadID,
    uint3 gtid    : SV_GroupThreadID,
    uint3 gid     : SV_GroupID,
    uint  gindex  : SV_GroupIndex
)
{
    float output_width, output_height;
    uav_brdf_lut.GetDimensions(output_width, output_height);

    #if 1
        // HLSLの場合roughnessが0に近い箇所でギャップが発生し黒いライン状になってしまうため, Y(roughness)のみ若干オフセット.
        const float2 uv = (float2(dtid.xy) + float2(0.5, 2.5)) / float2(output_width, output_height);
    #else
        const float2 uv = (float2(dtid.xy) + float2(0.5, 0.5)) / float2(output_width, output_height);
    #endif

    const float NoV = uv.x;
    const float perceptual_roughness = uv.y;

    const float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    const float3 N = float3(0.0, 0.0, 1.0);

    // IBL DFG LUT Importance Sampling.
    // https://google.github.io/filament/Filament.html
    float2 dfg_xy = float2(0.0, 0.0);
    for(int si = 0; si < GGX_BRDF_LUT_SAMPLING_COUNT; ++si)
    {
        const float2 xi = Hammersley2d(si, GGX_BRDF_LUT_SAMPLING_COUNT);
        
        const float3 H = ImportanceSampleHemisphereHalfVectorGGX(xi, perceptual_roughness);
        const float3 L = normalize(2.0 * dot(V, H) * H - V);

        const float NoL = max(L.z, 0.0);
        const float NoH = max(H.z, 0.0);
        const float VoH = max(dot(V, H), 0.0);

        if(NoL > 0.0)
        {
            const float roughness_a = perceptual_roughness*perceptual_roughness;
            float G = GDFG(NoV, NoL, roughness_a);
            float Gv = G * VoH / NoH;
            float Fc = pow(1.0 - VoH, 5.0);
            dfg_xy.x += Gv * (1.0 - Fc);
            dfg_xy.y += Gv * Fc;
        }
    }

    dfg_xy /= float(GGX_BRDF_LUT_SAMPLING_COUNT);
    uav_brdf_lut[dtid.xy] = float4(dfg_xy.x, dfg_xy.y, 0.0, 0.0);
}



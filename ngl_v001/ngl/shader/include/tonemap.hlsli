#ifndef NGL_SHADER_TONEMAP_H
#define NGL_SHADER_TONEMAP_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )


#define ngl_PI 3.14159265358979323846
#define ngl_EPSILON 1e-6

float CalcLuminance_Rec709(float3 input)
{
    return dot(input, float3(0.2126, 0.7152, 0.0722));
}


// Clamp (Simplest)
float3 Tonempa_Clamp(float3 input)
{
    return saturate(input);
}

// Reinhard. https://www-old.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf
// Target Luminance is Infinity
float3 Tonempa_Reinhard(float3 input)
{
    return input / (1.0 + input);
}
// Extented Reinhard. max white parameter. https://64.github.io/tonemapping/
// Output reaches 1 when input is max_white
float3 Tonempa_Reinhard_Extended(float3 input, float max_white)
{
    const float3 numerator = input * (1.0 + (input / (max_white*max_white)));
    return numerator / (1.0 + input);
}

float3 Tonemap_Uncharted2_Partial(float3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
// https://64.github.io/tonemapping/
float3 Tonemap_Uncharted2_Filmic(float3 v)
{
    float exposure_bias = 2.0f;
    float3 curr = Tonemap_Uncharted2_Partial(v * exposure_bias);

    float3 W = float3(11.2, 11.2, 11.2);
    float3 white_scale = float3(1.0, 1.0, 1.0) / Tonemap_Uncharted2_Partial(W);
    return curr * white_scale;
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 Tonemap_AcesFilm_Approx(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

#endif // NGL_SHADER_TONEMAP_H
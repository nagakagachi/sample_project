#ifndef NGL_SHADER_BRDF_H
#define NGL_SHADER_BRDF_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )

#include "math_util.hlsli"


//#define ngl_PI 3.14159265358979323846
#define ngl_EPSILON 1e-6

// GGXの知覚的Roughnessの下限制限をしてハイライトを残すための値.
// https://google.github.io/filament/Filament.html
#define ngl_MIN_PERCEPTUAL_ROUGHNESS 0.045


// reflectance : 誘電体の場合の正規化反射率[0,1]
float3 compute_F0(const float3 base_color, float metallic, float dielectric_reflectance) {
	return (base_color * metallic) + (0.16 * dielectric_reflectance * dielectric_reflectance * (1.0 - metallic));
}
// 誘電体の正規化反射率(reflectance)に一般的な値な値を採用して計算.
float3 compute_F0_default(const float3 base_color, float metallic) {
	const float k_dielectric_reflectance = 0.5;// compute_F0()の metallic==0 で一般的な誘電体のスペキュラF0である0.04になるような値.
	return compute_F0(base_color, metallic, k_dielectric_reflectance);
}

// HalfVectorの計算は初回フレームなどの特異な状況でNaNが発生することが多いため安全な正規化を用意.
float3 ngl_safe_normalize(float3 v)
{
	float vov = dot(v, v);
	return (0.0001 <= vov)? v*rsqrt(vov) : float3(0.0, 1.0, 0.0);
}

float3 brdf_schlick_F(float3 F0, float3 N, float3 V, float3 L)
{
	const float3 h = ngl_safe_normalize(V + L);
	const float v_o_h = saturate(dot(V, h));
	const float tmp = (1.0 - v_o_h);
	const float3 F = F0 + (1.0 - F0) * (tmp*tmp*tmp*tmp*tmp);
	return F;
}
float brdf_trowbridge_reitz_D(float perceptual_roughness, float3 N, float3 V, float3 L)
{
	const float limited_perceptual_roughness = clamp(perceptual_roughness, ngl_MIN_PERCEPTUAL_ROUGHNESS, 1.0);
	const float a = limited_perceptual_roughness*limited_perceptual_roughness;

	const float a2 = a*a;
	
	const float3 h = ngl_safe_normalize(V + L);
	const float n_o_h = dot(N, h);
	const float tmp = (1.0 + n_o_h*n_o_h * (a2 - 1.0));
	const float D = (a2) / (NGL_PI * tmp*tmp);
	return D;
}
// Height-Correlated Smith Masking-Shadowing
// マイクロファセットBRDFの分母 4*NoV*NoL の項を含み単純化されたもの.
float brdf_smith_ggx_correlated_G(float perceptual_roughness, float3 N, float3 V, float3 L)
{
	const float limited_perceptual_roughness = clamp(perceptual_roughness, ngl_MIN_PERCEPTUAL_ROUGHNESS, 1.0);
	const float a = limited_perceptual_roughness*limited_perceptual_roughness;
	const float a2 = a*a;
	
	const float ui = saturate(dot(N,L));
	const float uo = saturate(dot(N,V));
	
	const float d0 = uo * sqrt(a2 + ui * (ui - a2 * ui));
	const float d1 = ui * sqrt(a2 + uo * (uo - a2 * uo));
	const float G = 0.5 / (d0 + d1 + ngl_EPSILON);// zero divide対策.
	return G;
}
float3 brdf_standard_ggx(float3 base_color, float perceptual_roughness, float metalness, float3 N, float3 V, float3 L)
{
	const float3 F0 =  compute_F0_default(base_color, metalness);
	
	const float3 brdf_F = brdf_schlick_F(F0, N, V, L);
	const float brdf_D = brdf_trowbridge_reitz_D(perceptual_roughness, N, V, L);
	// マイクロファセットBRDFの分母の 4*NoV*NoL 項は式の単純化のためG項に含まれる.[Lagarde].
	const float brdf_G = brdf_smith_ggx_correlated_G(perceptual_roughness, N, V, L);

	return brdf_D * brdf_F * brdf_G;
}

float3 brdf_lambert(float3 base_color, float perceptual_roughness, float metalness, float3 N, float3 V, float3 L)
{
	const float lambert = 1.0 / NGL_PI;

	const float3 diffuse = lerp(base_color, 0.0, metalness) * lambert;
	return diffuse;
}



// ----------------------------------------------------------------------------------
// Sampling.

// Hammersley Sequence で利用.
// https://learnopengl.com/PBR/IBL/Specular-IBL
float RadicalInverse_VdC(uint bits) 
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// https://learnopengl.com/PBR/IBL/Specular-IBL
float2 Hammersley2d(uint i, uint N)
{
	return float2(float(i)/float(N), RadicalInverse_VdC(i));
}
//
// https://learnopengl.com/PBR/IBL/Specular-IBL
float3 ImportanceSampleHalfVectorGGX(float2 Xi, float3 N, float roughness)
{
	float a = roughness*roughness;
	
	float phi = 2.0 * NGL_PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
	// from spherical coordinates to cartesian coordinates
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;
	
	// from tangent-space vector to world-space sample vector
	float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent   = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);
	
	float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return sampleVec;
}  


#endif // NGL_SHADER_BRDF_H
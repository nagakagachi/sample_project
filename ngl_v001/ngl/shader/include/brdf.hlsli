#ifndef NGL_SHADER_BRDF_H
#define NGL_SHADER_BRDF_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )


#define ngl_PI 3.14159265358979323846
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


float3 brdf_schlick_F(float3 F0, float3 N, float3 V, float3 L)
{
	const float3 h = normalize(V + L);
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
	
	const float3 h = normalize(V + L);
	const float n_o_h = dot(N, h);
	const float tmp = (1.0 + n_o_h*n_o_h * (a2 - 1.0));
	const float D = (a2) / (ngl_PI * tmp*tmp);
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
	const float lambert = 1.0 / ngl_PI;

	const float3 diffuse = lerp(base_color, 0.0, metalness) * lambert;
	return diffuse;
}


#endif // NGL_SHADER_BRDF_H
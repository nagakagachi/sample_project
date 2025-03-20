#ifndef NGL_SHADER_MATH_UTIL_H
#define NGL_SHADER_MATH_UTIL_H

float Matrix2x2_Determinant(float2x2 m)
{
    return m._m00 * m._m11 - m._m01 * m._m10;
}
float Matrix3x3_Determinant(float3x3 m)
{
    const float c00 = Matrix2x2_Determinant(float2x2(m._m11, m._m12, m._m21, m._m22));
    const float c01 = -Matrix2x2_Determinant(float2x2(m._m10, m._m12, m._m20, m._m22));
    const float c02 = Matrix2x2_Determinant(float2x2(m._m10, m._m11, m._m20, m._m21));
    return m._m00 * c00 + m._m01 * c01 + m._m02 * c02;
}
// 余因子行列 (for normal transform).
// https://github.com/graphitemaster/normals_revisited .
float2x2 Matrix2x2_Cofactor(float2x2 m)
{
    const float c00 = m._m11;
    const float c01 = -m._m10;
    const float c10 = -m._m01;
    const float c11 = m._m00;
    return float2x2(
        c00, c01,
        c10, c11
    );
}

// 余因子行列 (for normal transform).
//  https://github.com/graphitemaster/normals_revisited .
float3x3 Matrix3x3_Cofactor(float3x3 m)
{
    const float c00 = Matrix2x2_Determinant(float2x2(m._m11, m._m12, m._m21, m._m22));
    const float c01 = -Matrix2x2_Determinant(float2x2(m._m10, m._m12, m._m20, m._m22));
    const float c02 = Matrix2x2_Determinant(float2x2(m._m10, m._m11, m._m20, m._m21));

    const float c10 = -Matrix2x2_Determinant(float2x2(m._m01, m._m02, m._m21, m._m22));
    const float c11 = Matrix2x2_Determinant(float2x2(m._m00, m._m02, m._m20, m._m22));
    const float c12 = -Matrix2x2_Determinant(float2x2(m._m00, m._m01, m._m20, m._m21));

    const float c20 = Matrix2x2_Determinant(float2x2(m._m01, m._m02, m._m11, m._m12));
    const float c21 = -Matrix2x2_Determinant(float2x2(m._m00, m._m02, m._m10, m._m12));
    const float c22 = Matrix2x2_Determinant(float2x2(m._m00, m._m01, m._m10, m._m11));

    return float3x3(
        c00, c01, c02,
        c10, c11, c12,
        c20, c21, c22
    );
}

#endif
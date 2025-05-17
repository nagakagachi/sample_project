#ifndef NGL_SHADER_MATH_UTIL_H
#define NGL_SHADER_MATH_UTIL_H

#define NGL_PI 3.141592653589793

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
float Matrix4x4_Determinant(float4x4 m)
{
    const float c00 = Matrix3x3_Determinant(float3x3(m._m11, m._m12, m._m13, m._m21, m._m22, m._m23, m._m31, m._m32, m._m33));
    const float c01 = -Matrix3x3_Determinant(float3x3(m._m10, m._m12, m._m13, m._m20, m._m22, m._m23, m._m30, m._m32, m._m33));
    const float c02 = Matrix3x3_Determinant(float3x3(m._m10, m._m11, m._m13, m._m20, m._m21, m._m23, m._m30, m._m31, m._m33));
    const float c03 = -Matrix3x3_Determinant(float3x3(m._m10, m._m11, m._m12, m._m20, m._m21, m._m22, m._m30, m._m31, m._m32));
    return m._m00 * c00 + m._m01 * c01 + m._m02 * c02 + m._m03 * c03;
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

// 余因子行列 (for normal transform).
//  https://github.com/graphitemaster/normals_revisited .
float4x4 Matrix4x4_Cofactor(float4x4 m)
{
    const float c00 = Matrix3x3_Determinant(float3x3(m._m11, m._m12, m._m13, m._m21, m._m22, m._m23, m._m31, m._m32, m._m33));
    const float c01 = -Matrix3x3_Determinant(float3x3(m._m10, m._m12, m._m13, m._m20, m._m22, m._m23, m._m30, m._m32, m._m33));
    const float c02 = Matrix3x3_Determinant(float3x3(m._m10, m._m11, m._m13, m._m20, m._m21, m._m23, m._m30, m._m31, m._m33));
    const float c03 = -Matrix3x3_Determinant(float3x3(m._m10, m._m11, m._m12, m._m20, m._m21, m._m22, m._m30, m._m31, m._m32));
    
    const float c10 = -Matrix3x3_Determinant(float3x3(m._m01, m._m02, m._m03, m._m21, m._m22, m._m23, m._m31, m._m32, m._m33));
    const float c11 = Matrix3x3_Determinant(float3x3(m._m00, m._m02, m._m03, m._m20, m._m22, m._m23, m._m30, m._m32, m._m33));
    const float c12 = -Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m03, m._m20, m._m21, m._m23, m._m30, m._m31, m._m33));
    const float c13 = Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m02, m._m20, m._m21, m._m22, m._m30, m._m31, m._m32));
    
    const float c20 = Matrix3x3_Determinant(float3x3(m._m01, m._m02, m._m03, m._m11, m._m12, m._m13, m._m31, m._m32, m._m33));
    const float c21 = -Matrix3x3_Determinant(float3x3(m._m00, m._m02, m._m03, m._m10, m._m12, m._m13, m._m30, m._m32, m._m33));
    const float c22 = Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m03, m._m10, m._m11, m._m13, m._m30, m._m31, m._m33));
    const float c23 = -Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m02, m._m10, m._m11, m._m12, m._m30, m._m31, m._m32));
    
    const float c30 = -Matrix3x3_Determinant(float3x3(m._m01, m._m02, m._m03, m._m11, m._m12, m._m13, m._m21, m._m22, m._m23));
    const float c31 = Matrix3x3_Determinant(float3x3(m._m00, m._m02, m._m03, m._m10, m._m12, m._m13, m._m20, m._m22, m._m23));
    const float c32 = -Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m03, m._m10, m._m11, m._m13, m._m20, m._m21, m._m23));
    const float c33 = Matrix3x3_Determinant(float3x3(m._m00, m._m01, m._m02, m._m10, m._m11, m._m12, m._m20, m._m21, m._m22));
    
    return float4x4(
        c00, c01, c02, c03,
        c10, c11, c12, c13,
        c20, c21, c22, c23,
        c30, c31, c32, c33
    );
}


// スクリーンピクセルへのView空間レイベクトルを計算
//  ピクセルのスクリーン上UVとProjection行列から計算する.
//  このレイベクトルとViewZを利用してView空間座標を復元する場合は, zが1になるように修正して計算が必要な点に注意( pos_vs.z == ViewZ となるようにする必要がある).
//		pos_view = ViewSpaceRay.xyz/abs(ViewSpaceRay.z) * ViewZ
float3 CalcViewSpaceRay(float2 screen_uv, float4x4 proj_mtx)
{
    float2 ndc_xy = (screen_uv * 2.0 - 1.0) * float2(1.0, -1.0);
    // 逆行列を使わずにProj行列の要素からレイ方向計算.
    const float inv_tan_horizontal = proj_mtx._m00; // m00 = 1/tan(fov_x*0.5)
    const float inv_tan_vertical = proj_mtx._m11; // m11 = 1/tan(fov_y*0.5)
	
    const float3 ray_dir_view = normalize( float3(ndc_xy.x / inv_tan_horizontal, ndc_xy.y / inv_tan_vertical, 1.0) );
    // View空間でのRay方向. World空間へ変換する場合は InverseViewMatrix * ray_dir_view とすること.
    return ray_dir_view;
}

// ワールド空間レイ方向からパノラマイメージUVへのマッピング.
float2 CalcPanoramaTexcoordFromWorldSpaceRay(float3 ray_dir)
{
    const float2 panorama_uv = float2((atan2(-ray_dir.x, -ray_dir.z) / (NGL_PI)) * 0.5 + 0.5, acos(ray_dir.y) / NGL_PI);
    return panorama_uv;
}

#endif
#ifndef NGL_SHADER_MATH_UTIL_H
#define NGL_SHADER_MATH_UTIL_H

#define NGL_PI (3.141592653589793)
#define NGL_2PI (2.0*NGL_PI)
#define NGL_HALF_PI (0.5*NGL_PI)

#define NGL_EPSILON 0.00001

//--------------------------------------------------------------------------------
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

// vec3を符号1bit+9bit整数 * 3のu32にエンコード/デコード.
uint encode_10bit_int_vector3_to_u32(int3 v)
{
    // 各成分を10bit符号付き整数に変換してu32にパック.
    const uint3 component_sign = select(0 > v, 1, 0);
    v = abs(v);
    uint code_x = (v.x & 0x1ff) | (component_sign.x << 9);
    uint code_y = (v.y & 0x1ff) | (component_sign.y << 9);
    uint code_z = (v.z & 0x1ff) | (component_sign.z << 9);
    return (code_x | (code_y << 10) | (code_z << 20));
}
// vec3を符号1bit+9bit整数 * 3のu32にエンコード/デコード.
int3 decode_10bit_int_vector3_from_u32(uint code)
{
    int3 v;
    v.x = int(code & 0x1ff);
    v.y = int((code >> 10) & 0x1ff);
    v.z = int((code >> 20) & 0x1ff);
    const uint3 component_sign = uint3((code >> 9) & 0x1, (code >> 19) & 0x1, (code >> 29) & 0x1);
    v = select(component_sign, v, -v);
    return v;
}

// マンハッタン距離.
int length_int_vector3(int3 a)
{
    int3 d = abs(a);
    return d.x + d.y + d.z;
}
// マンハッタン距離.
int distance_int_vector3(int3 a, int3 b)
{
    return length_int_vector3(a - b);
}




//--------------------------------------------------------------------------------
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

// CubemapのPlane[0,5]に対応する向きベクトルを取得.
void GetCubemapPlaneAxis(int cube_plane_index, out float3 out_front, out float3 out_up, out float3 out_right)
{
    const float3 plane_axis_front[6] = {
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0), float3(0.0, -1.0, 0.0),
        float3(0.0, 0.0, 1.0), float3(0.0, 0.0, -1.0),
    };
    const float3 plane_axis_up[6] = {
        float3(0.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, -1.0), float3(0.0, 0.0, 1.0),
        float3(0.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
    };
    const float3 plane_axis_right[6] = {
        float3(0.0, 0.0, -1.0), float3(0.0, 0.0, 1.0),
        float3(1.0, 0.0, 0.0), float3(1.0, 0.0, 0.0),
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
    };

    out_front = plane_axis_front[cube_plane_index];
    out_up = plane_axis_up[cube_plane_index];
    out_right = plane_axis_right[cube_plane_index];
}

//--------------------------------------------------------------------------------
// 最大の値を取る軸の軸ベクトルを返す(入力ベクトルは正のみ).
int3 calc_principal_axis(float3 v)
{
    if(v.x >= v.y && v.x >= v.z) return int3(1,0,0);
    if(v.y >= v.x && v.y >= v.z) return int3(0,1,0);
    return int3(0,0,1);
}
// 最大の値を取る軸のComponentIndexを返す(入力ベクトルは正のみ).
int calc_principal_axis_component_index(float3 v)
{
    if(v.x >= v.y && v.x >= v.z) return 0;
    if(v.y >= v.x && v.y >= v.z) return 1;
    return 2;
}


//--------------------------------------------------------------------------------
// ランダム
uint noise_iqint32_orig(uint2 p)  
{  
    p *= uint2(73333, 7777);  
    p ^= uint2(3333777777, 3333777777) >> (p >> 28);  
    uint n = p.x * p.y;  
    return n ^ n >> 15;  
}
float noise_iqint32(float pos)  
{  
    uint value = noise_iqint32_orig(asuint(pos.xx));  
    return value * 2.3283064365386962890625e-10;  
}
float noise_iqint32(float2 pos)  
{  
    uint value = noise_iqint32_orig(asuint(pos.xy));  
    return value * 2.3283064365386962890625e-10;  
}
float noise_iqint32(float3 pos)  
{  
    uint value = noise_iqint32_orig(asuint(pos.xy)) + noise_iqint32_orig(asuint(pos.zz));
    return value * 2.3283064365386962890625e-10;  
}
float noise_iqint32(float4 pos)  
{  
    uint value = noise_iqint32_orig(asuint(pos.xy)) + noise_iqint32_orig(asuint(pos.zw));  
    return value * 2.3283064365386962890625e-10;  
}

float3 random_unit_vector3(float2 seed)
{
    const float angleY = noise_iqint32(seed.xyxy) * NGL_2PI;
    const float angleX = asin(noise_iqint32(seed.yyxx)*2.0 - 1.0);
    float3 sample_ray_dir;
    sample_ray_dir.y = sin(angleX);
    sample_ray_dir.x = cos(angleX) * cos(angleY);
    sample_ray_dir.z = cos(angleX) * sin(angleY);
    return sample_ray_dir;
}

// Fibonacci球面分布方向を取得.
// indexのmoduloは呼び出し側の責任とする.
float3 fibonacci_sphere_point(uint index, uint sample_count_max)
{
    const float phi = 3.14159265359 * (3.0 - sqrt(5.0)); // 黄金角
    const float y = 1.0 - (index / float(sample_count_max - 1)) * 2.0;// ここで 1 になると後段の sqrt に 0.0 が入って計算破綻する.
    const float horizontal_radius = sqrt((1.0 - y * y) + NGL_EPSILON);// sqrtに1が入らないようにするための安全策として加算で済ませるパターン.
    const float theta = phi * index;

    const float x = cos(theta) * horizontal_radius;
    const float z = sin(theta) * horizontal_radius;
    return float3(x, y, z);
}



// Octahedron Mapping.
float2 OctWrap(float2 v)
{
    //return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}
float2 OctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
} 
float3 OctDecode(float2 f)
{
    f = f * 2.0 - 1.0;
 
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    //n.xy += n.xy >= 0.0 ? -t : t;
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}


#endif
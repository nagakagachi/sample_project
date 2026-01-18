#ifndef NGL_SHADER_MATH_UTIL_H
#define NGL_SHADER_MATH_UTIL_H
#include "ngl_shader_config.hlsli"

#define NGL_PI (3.141592653589793)
#define NGL_2PI (2.0*NGL_PI) // Tau
#define NGL_HALF_PI (0.5*NGL_PI)

#define NGL_PHI (1.618034033988749895) // 黄金比
#define NGL_GOLDEN_ANGLE (NGL_2PI * (2.0 - NGL_PHI)) // 2.39996 radians

#define NGL_EPSILON 0.00001



float Max3(float3 v) { return max(v.x, max(v.y, v.z)); }
float Max3(float a, float b, float c) { return max(a, max(b, c)); }
float Min3(float3 v) { return min(v.x, min(v.y, v.z)); }
float Min3(float a, float b, float c) { return min(a, min(b, c)); }

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

// 二乗値計算.
float CalcSquare(float v)
{
    return v * v;
}



// View逆行列からカメラ向きベクトルを取得.
float3 GetViewDirFromInverseViewMatrix(float3x4 view_inv_mtx)
{
    return normalize(view_inv_mtx._m02_m12_m22);
}
// View逆行列からカメラUpDirベクトルを取得.
float3 GetViewUpDirFromInverseViewMatrix(float3x4 view_inv_mtx)
{
    return normalize(view_inv_mtx._m01_m11_m21);
}
// View逆行列からカメラRightDirベクトルを取得.
float3 GetViewRightDirFromInverseViewMatrix(float3x4 view_inv_mtx)
{
    return normalize(view_inv_mtx._m00_m10_m20);
}
// View逆行列からカメラ座標を取得.
float3 GetViewOriginFromInverseViewMatrix(float3x4 view_inv_mtx)
{
    return view_inv_mtx._m03_m13_m23;
}

// Projection行列がReverseなら真.
bool IsReverseProjectionMatrix(float4x4 proj_mtx)
{
    // [2][3]が正ならReverseZ.
    return proj_mtx._m23 > 0.0;
}
// Projection行列からReverseZモードも考慮してNear,FarのDepth値(0, 1)を取得. Reverseなら(1,0), Standardなら(0, 1).
float2 GetNearFarPlaneDepthFromProjectionMatrix(float4x4 proj_mtx)
{
    return IsReverseProjectionMatrix(proj_mtx)? float2(1.0, 0.0) : float2(0.0, 1.0);
}

// fromベクトルをtoベクトルへ回転する行列を計算
// 参考: "The Shortest Arc Quaternion" by Stan Melax
float3x3 RotFromToMatrix(float3 from, float3 to)
{
    const float3 v = cross(from, to);
    const float c = dot(from, to);
    const float k = 1.0 / (1.0 + c);
    
    // v * v^T * k の各要素を計算して回転行列を直接構築
    return float3x3(
        v.x * v.x * k + c,      v.x * v.y * k - v.z,    v.x * v.z * k + v.y,
        v.y * v.x * k + v.z,    v.y * v.y * k + c,      v.y * v.z * k - v.x,
        v.z * v.x * k - v.y,    v.z * v.y * k + v.x,    v.z * v.z * k + c
    );
}

//--------------------------------------------------------------------------------
// スクリーンピクセルへのView空間レイベクトルを計算. Perspective用.
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

//--------------------------------------------------------------------------------
// スクリーンピクセルのView空間座標を計算. Perspective Otrtho両対応.
//  CalcViewSpaceRay()はPerspective用であるため, MainViewとShadowMap等のOrthoもあり得るコードで座標復元をする場合に利用.
float3 CalcViewSpacePosition(float2 screen_uv, float view_z, float4x4 proj_mtx)
{
    /* Orthoでは動作 Perspectiveでは不具合あり.
    float2 ndc_xy = (screen_uv * 2.0 - 1.0) * float2(1.0, -1.0);
    const float inv_tan_horizontal = proj_mtx._m00; // m00 = 1/tan(fov_x*0.5)
    const float inv_tan_vertical = proj_mtx._m11; // m11 = 1/tan(fov_y*0.5)
    const float offset_horizontal = -proj_mtx._m03 * (1.0 / inv_tan_horizontal); // m03 = -(right + left)/(right - left)
    const float offset_vertical = -proj_mtx._m13 * (1.0 / inv_tan_vertical);   // m13 = -(top + bottom)/(top - bottom)

    return float3(ndc_xy.x / inv_tan_horizontal + offset_horizontal, ndc_xy.y / inv_tan_vertical + offset_vertical, view_z);
    */
   
    float2 ndc_xy = (screen_uv * 2.0 - 1.0) * float2(1.0, -1.0);
    const float tan_horizontal = 1.0/proj_mtx._m00; // m00 = 1/tan(fov_x*0.5)
    const float tan_vertical = 1.0/proj_mtx._m11; // m11 = 1/tan(fov_y*0.5)
    const float offset_horizontal = -proj_mtx._m03 * tan_horizontal; // m03 = -(right + left)/(right - left)
    const float offset_vertical = -proj_mtx._m13 * tan_vertical;   // m13 = -(top + bottom)/(top - bottom)
    const float relation_perspective = (0.0 == proj_mtx._m33)? view_z : 1.0; // m33 = 0 for Perspective, 1 for Ortho

    return float3((ndc_xy.x * relation_perspective) * tan_horizontal + offset_horizontal, (ndc_xy.y * relation_perspective) * tan_vertical + offset_vertical, view_z);
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
// ベクトルの各成分が昇順で何番目の絶対値の大きさかを格納したVec3iを返す.
// 例えば入力Vec3( -5.0f, 2.0f, 3.0f )ならば、絶対値の大きさ順は (2, 0, 1) なので、戻り値は Vec3i(2,0,1) となる.
int3 GetVec3ComponentOrderByMagnitude(float3 v)
{
    v = abs(v);
    const int x_order = int(v.x > v.y) + int(v.x > v.z);
    const int y_order = int(v.y >= v.x) + int(v.y > v.z);
    const int z_order = int(v.z >= v.x) + int(v.z >= v.y);
    return int3(x_order, y_order, z_order);
}
// ベクトルの各成分が昇順で並べ直すためのインデックスベクトルを返す.
// 例えば入力Vec3( -5.0f, 2.0f, 3.0f )ならば、絶対値の大きさ順は (2, 0, 1) なので、戻り値は Vec3i(1,2,0) となる.
int3 GetVec3ComponentReorderIndexByMagnitude(float3 v)
{
    const int3 order = GetVec3ComponentOrderByMagnitude(v);
    int3 index_vec;
    index_vec[order.x] = 0;
    index_vec[order.y] = 1;
    index_vec[order.z] = 2;
    return index_vec;
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
float3 fibonacci_sphere_point(int index, int sample_count_max)
{
    const float phi = NGL_GOLDEN_ANGLE;//NGL_PI * (3.0 - sqrt(5.0)); // 黄金角
    const float y = 1.0 - (index / float(sample_count_max - 1)) * 2.0;// ここで 1 になると後段の sqrt に 0.0 が入って計算破綻する.
    const float horizontal_radius = sqrt((1.0 - y * y) + NGL_EPSILON);// sqrtに1が入らないようにするための安全策として加算で済ませるパターン.
    const float theta = phi * index;
    const float x = cos(theta) * horizontal_radius;
    const float z = sin(theta) * horizontal_radius;
    return float3(x, y, z);
}
// 密度一定のFibonacci螺旋分布で2D点列を計算.
float2 fibonacci_spiral_point(int index, int sample_count_max, float angle_offset)
{
    const float2 sample_dir = cos(float(index) * NGL_GOLDEN_ANGLE + angle_offset + float2(0, NGL_HALF_PI));// cos, sin は90度オフセットで得られる.
    const float2 sample_offset = sample_dir * sqrt(float(index)/float(sample_count_max));
    return sample_offset;
}
// 中心に偏りのあるFibonacci螺旋分布で2D点列を計算.
float2 fibonacci_spiral_point_sq(int index, int sample_count_max, float angle_offset)
{
    const float2 sample_dir = cos(float(index) * NGL_GOLDEN_ANGLE + angle_offset + float2(0, NGL_HALF_PI));// cos, sin は90度オフセットで得られる.
    const float2 sample_offset = sample_dir * (float(index)/float(sample_count_max));
    return sample_offset;
}

// Interleaved Gradient Noise
// テクセル座標から疑似ランダムノイズを生成
// Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float InterleavedGradientNoise(float2 pixel_coord)
{
    const float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel_coord, magic.xy)));
}
// Gold Noise
// https://www.shadertoy.com/view/ltB3zD
// Optimized hash function for golden ratio based noise
// Divergentな値で特定の値(994, 581)などを与えるとNaNになる謎の不具合があるため注意.
float GoldNoise(float2 xy, float seed)
{
    return frac(tan(distance(xy * NGL_PHI, xy) * seed) * xy.x);
}

float GoldNoise(float2 xy)
{
    return GoldNoise(xy, 1.0);
}

float GoldNoise(float3 xyz, float seed)
{
    return frac(tan(distance(xyz.xy * NGL_PHI, xyz.yz) * seed) * xyz.x);
}

float GoldNoise(float3 xyz)
{
    return GoldNoise(xyz, 1.0);
}


// Octahedron Mapping.
float2 OctWrap(float2 v)
{
    //return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}
// 1,0,0 のような基底ベクトルの場合に結果のUVが 1,0 等になるため, テクセル座標として利用する場合はclampするなど注意が必要.
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
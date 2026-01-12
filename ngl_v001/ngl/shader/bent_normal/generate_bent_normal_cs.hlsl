
#if 0

generate_bent_normal_cs.hlsl

GI用のBentNormal計算. 高周波AO用ではなく低周波GIプローブの補助用.

#endif

#include "../include/math_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

Texture2D			TexLinearDepth;

RWTexture2D<float4>	RWTexBentNormal;


void CalcViewSpaceTangentAndNormalFromLinearDepth(out float3 out_tangent_x, out float3 out_tangent_y, out float3 out_normal, float2 uv, int2 texel_pos, float linear_depth, Texture2D TexLinearDepth, float2 texture_size_inv)
{
    float view_z_right = TexLinearDepth.Load(int3(texel_pos + int2(1, 0), 0)).r;
    float view_z_down = TexLinearDepth.Load(int3(texel_pos + int2(0, 1), 0)).r;

    view_z_right = (view_z_right > 65535.0) ? linear_depth : view_z_right;
    view_z_down = (view_z_down > 65535.0) ? linear_depth : view_z_down;
    
    const float3 view_pos = CalcViewSpacePosition(uv, linear_depth, cb_ngl_sceneview.cb_proj_mtx);
    const float3 view_pos_right = CalcViewSpacePosition(uv + float2(texture_size_inv.x, 0), view_z_right, cb_ngl_sceneview.cb_proj_mtx);
    const float3 view_pos_down = CalcViewSpacePosition(uv + float2(0, texture_size_inv.y), view_z_down, cb_ngl_sceneview.cb_proj_mtx);
    
    out_tangent_x = normalize(view_pos_right - view_pos);
    out_tangent_y = normalize(view_pos_down - view_pos);
    out_normal = normalize(cross(out_tangent_x, out_tangent_y));
}

// A * x = b を解く 3x3 線形方程式ソルバ
float3 SolveLinear3(float3x3 A, float3 b)
{
    float a00 = A[0][0]; float a01 = A[1][0]; float a02 = A[2][0];
    float a10 = A[0][1]; float a11 = A[1][1]; float a12 = A[2][1];
    float a20 = A[0][2]; float a21 = A[1][2]; float a22 = A[2][2];

    float b0 = b.x;
    float b1 = b.y;
    float b2 = b.z;

    // --- 前進消去 ---
    // pivot 0 (行0, 列0). ここでは単純化のためピボット選択はしていない。
    float invPivot0 = 1.0 / a00;
    a01 *= invPivot0;
    a02 *= invPivot0;
    b0  *= invPivot0;

    // R1 <- R1 - a10 * R0
    a11 -= a10 * a01;
    a12 -= a10 * a02;
    b1  -= a10 * b0;

    // R2 <- R2 - a20 * R0
    a21 -= a20 * a01;
    a22 -= a20 * a02;
    b2  -= a20 * b0;

    // pivot 1 (行1, 列1)
    float invPivot1 = 1.0 / a11;
    a12 *= invPivot1;
    b1  *= invPivot1;

    // R2 <- R2 - a21 * R1
    a22 -= a21 * a12;
    b2  -= a21 * b1;

    // pivot 2 (行2, 列2)
    float invPivot2 = 1.0 / a22;
    b2  *= invPivot2;

    // --- 後退代入 ---
    float x2 = b2;
    float x1 = b1 - a12 * x2;
    float x0 = b0 - a01 * x1 - a02 * x2;

    return float3(x0, x1, x2);
}
// 逆べき乗法(Inverse Power Method)による最小固有値に対応する固有ベクトルの近似計算
// A: 対称 3x3 共分散行列
// v0: 初期ベクトル（正規化済み, 真の解に近いほど収束が早い）
// maxIterations : 反復回数, 5程度で十分な精度が得られることが多い. 3〜8回程度で調整.
float3 CalcInversePowerSmallestEigenvector(float3x3 A, float3 v0, const int maxIterations)
{
    // 初期ベクトルを正規化
    float3 v = normalize(v0);

    // 反復回数は用途次第で 3〜8 回程度。
    // 品質とコストのバランスを見て調整。
    const int kNumIterations = 5;

    [unroll]
    for (int i = 0; i < maxIterations; ++i)
    {
        // A * x = v を解く → x = A^-1 * v に相当
        float3 x = SolveLinear3(A, v);

        // 正規化して次の v に
        v = normalize(x);
    }

    return v;  // 最小固有値に対応する固有ベクトルの近似
}
// Rayleigh 商による固有値の近似計算
float SmallestEigenvalueRayleigh(float3x3 A, float3 eigenVec)
{
    float3 Av = mul(A, eigenVec);   // 列ベクトル想定なら mul(A, v)
    return dot(eigenVec, Av);       // Rayleigh quotient
}


[numthreads(8, 8, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
    uint w, h;
    TexLinearDepth.GetDimensions(w, h);
    const float2 texel_size = 1.0 / float2(w, h);
    const float2 texel_uv = float2(dtid.xy) * texel_size;

    // サンプル方向Jitter.
    const float phi_jitter = noise_iqint32(float2(dtid.xy)) * NGL_2PI;

    const float view_z = TexLinearDepth.Load(int3(dtid.xy, 0)).r;
    const float3 view_pos = CalcViewSpacePosition(texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);

    // 復元Tangent and Normal.
    float3 view_tangent_x, view_tangent_y, view_normal;
    CalcViewSpaceTangentAndNormalFromLinearDepth(view_tangent_x, view_tangent_y, view_normal, texel_uv, dtid.xy, view_z, TexLinearDepth, texel_size);


    // 出力用.
    float visibility = 0.0;
    float3 bent_normal = float3(0.0, 0.0, 0.0);

        if(view_z <= 65535.0)
        {
            const int sample_count = 16;
            const float sample_radius = 128.0;// Probeサンプル位置オフセット用途ではかなり粗い半径設定が望ましい.
            // 周辺テクセルView座標の分散共分散行列(の上三角成分).
            float3 covar_xx_xy_xz = float3(0.0, 0.0, 0.0);
            float3 covar_yy_yz_zz = float3(0.0, 0.0, 0.0);
            int sample_num = 0;
            for(int sample_i = 0; sample_i < sample_count; sample_i++)
            {
                const float2 omega = fibonacci_spiral_point(sample_i+1, sample_count, phi_jitter);

                const float2 sample_offset = float2(omega.x, omega.y) * sample_radius;
                const int2 sample_pos = int2(dtid.xy) + int2(sample_offset);
                const float2 sample_uv = texel_uv + sample_offset * texel_size;
                const float sample_view_z = TexLinearDepth.Load(int3(sample_pos, 0)).r;

                if(sample_view_z > 65535.0)
                    continue;

                const float3 sample_view_pos = CalcViewSpacePosition(sample_uv, sample_view_z, cb_ngl_sceneview.cb_proj_mtx);

                const float3 to_sample_vec = sample_view_pos - view_pos;

                // 共分散の逐次更新. 対称行列の上三角部のみ.
                sample_num = sample_num + 1;
                const int sample_num_next = sample_num + 1;
                {
                    const float3 covar_term_xx_xy_xz = float3(to_sample_vec.x*to_sample_vec.x, to_sample_vec.x*to_sample_vec.y, to_sample_vec.x*to_sample_vec.z);
                    const float3 covar_term_yy_yz_zz = float3(to_sample_vec.y*to_sample_vec.y, to_sample_vec.y*to_sample_vec.z, to_sample_vec.z*to_sample_vec.z);
                    const float progressive_factor_0  = (sample_num/float(sample_num_next*sample_num_next));
                    const float progressive_factor_1 = (sample_num/float(sample_num_next));

                    covar_xx_xy_xz = progressive_factor_0 * covar_term_xx_xy_xz + progressive_factor_1 * covar_xx_xy_xz;
                    covar_yy_yz_zz = progressive_factor_0 * covar_term_yy_yz_zz + progressive_factor_1 * covar_yy_yz_zz;
                }                
            }

            // 共分散行列構築.
            if(0 < sample_num)
            {
                float3x3 covar_mat;
                covar_mat[0] = float3(covar_xx_xy_xz.x, covar_xx_xy_xz.y, covar_xx_xy_xz.z);
                covar_mat[1] = float3(covar_xx_xy_xz.y, covar_yy_yz_zz.x, covar_yy_yz_zz.y);
                covar_mat[2] = float3(covar_xx_xy_xz.z, covar_yy_yz_zz.y, covar_yy_yz_zz.z);
                // 最小固有値に対応する固有ベクトル計算.
                // 初期値は正規化済みベクトル. 反復処理の起点であるため真の解に近いほうが収束が早い.
                // 初期値としてカメラ方向ベクトル. スクリーンスペースで低周波なBentNormalを求めたい場合はジオメトリ法線よりこちらのほうがよさそう.
                float3 init_eigen_vec = normalize(-view_pos);

                // 逆べき乗法反復で近似計算.
                float3 smallest_eigen_vec = CalcInversePowerSmallestEigenvector(covar_mat, init_eigen_vec, 5);


                bent_normal = smallest_eigen_vec;
                // to world space.
                bent_normal = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(bent_normal, 0.0)).xyz;

                // 可視化用.
                //bent_normal = bent_normal * 0.5 + 0.5;
            }
        }

    // 出力.
    RWTexBentNormal[dtid.xy] = float4(bent_normal, visibility);
}
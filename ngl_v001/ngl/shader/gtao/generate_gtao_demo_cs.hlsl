
#if 0

generate_gtao_demo_cs.hlsl

GTAOと追加のBentNormal計算のデモ.
論文の疑似コードそのままの実装. 距離減衰やエッジ棄却などは含まれていないプレーン実装.

Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion
https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf


#endif

#include "../include/math_util.hlsli"
// SceneView定数バッファ構造定義.
#include "../include/scene_view_struct.hlsli"

ConstantBuffer<SceneViewInfo> cb_ngl_sceneview;

Texture2D			TexLinearDepth;
RWTexture2D<float4>	RWTexGtaoBentNormal;// bent normal.xyz, visibility.w


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


    const float view_z = TexLinearDepth.Load(int3(dtid.xy, 0)).r;
    if(view_z > 65535.0)
    {
        // 有効な深度値がない場合は出力しない.
        RWTexGtaoBentNormal[dtid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 view_pos = CalcViewSpacePosition(texel_uv, view_z, cb_ngl_sceneview.cb_proj_mtx);

    float3 view_tangent_x, view_tangent_y, view_normal;
    CalcViewSpaceTangentAndNormalFromLinearDepth(view_tangent_x, view_tangent_y, view_normal, texel_uv, dtid.xy, view_z, TexLinearDepth, texel_size);

    // 出力用.
    float visibility = 0.0;
    float3 bent_normal = float3(0.0, 0.0, 0.0);

    const int slice_count = 8;
    const int direction_sample_count = 8;
    const float sample_radius_texel = 32.0;

    const float3 cPosV = view_pos;
    const float3 viewV = normalize(-cPosV);
    const float3x3 rotMatrixToViewV = RotFromToMatrix(float3(0.0, 0.0, -1.0), viewV);

    // サンプル方向Jitter.
    //const float phi_jitter = GoldNoise(float2(dtid.xy)) * NGL_2PI;// なぜか不正ピクセルが発生する? Divergentな値で特定の値(994, 581)などを与えるとNaNになる謎の不具合があるため注意.
    const float phi_jitter = noise_iqint32(float2(dtid.xy)) * NGL_2PI;// こちらは安定.

    for(int slice = 0; slice < slice_count; slice++)
    {
        const float phi = ((NGL_PI / float(slice_count)) * float(slice)) + phi_jitter;
        
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 directionV = float3(omega.x, omega.y, 0.0);
        const float3 orthoDirectionV = directionV - viewV * dot(directionV, viewV);// 表面法線平面へ投影したサンプル方向ベクトル.
        const float3 axisV = cross(directionV, viewV);// Sliceプレーン法線.
        const float3 projNormalV = view_normal - axisV * dot(view_normal, axisV);// 表面法線をSliceプレーンへ投影したベクトル.
        const float sgnN = sign(dot(orthoDirectionV, projNormalV));
        const float cosN = saturate(dot(projNormalV, viewV) / length(projNormalV));// projNormalVが正規化されていないためlengthで割る.
        const float projNormal_view_angle = sgnN * acos(cosN);// Sliceプレーン投影法線とビュー方向のなす角度.
        float h[2] = {0.0, 0.0};
        
        // 双方向探索.
        for(int side = 0; side < 2; side++)
        {
            // Slice毎に最大HorizonAngle探索.
            float cHorizonCos = -1.0;
            for(int sample_i = 0; sample_i < direction_sample_count; sample_i++)
            {
                const float s = float(sample_i+1) / float(direction_sample_count);
                const float2 sTexCoordOffset = float2(omega.x, -omega.y) * (-1.0 + 2.0 * float(side)) * s * sample_radius_texel;
                const int2 sTexelPos = int2(dtid.xy) + int2(sTexCoordOffset);
                const float2 sTexCoord = texel_uv + sTexCoordOffset * texel_size;
                const float sampleDepth = TexLinearDepth.Load(int3(sTexelPos, 0)).r;
                const float3 sPosV = CalcViewSpacePosition(sTexCoord, sampleDepth, cb_ngl_sceneview.cb_proj_mtx);
                const float3 sHorizonV = normalize(sPosV - cPosV);
                cHorizonCos = max(cHorizonCos, dot(sHorizonV, viewV));
            }
            // 最大HorizonAngle更新.
            h[side] = projNormal_view_angle + clamp((-1.0 + 2.0 * float(side)) * acos(cHorizonCos) - projNormal_view_angle, -NGL_HALF_PI, NGL_HALF_PI);
            // 式(7)のAO積分寄与(両sideの片方ずつ).
            visibility += length(projNormalV) * (cosN + 2.0 * h[side] * sin(projNormal_view_angle) - cos(2.0 * h[side] - projNormal_view_angle)) / 4.0;
        }

        // bent normal計算.
        {
            const float n = projNormal_view_angle;
            const float t0 = (6.0 * sin(h[0] - n) - sin(3.0 * h[0] - n) + 6.0 * sin(h[1] - n) - sin(3.0 * h[1] - n) + 16.0 * sin(n) - 3.0 * (sin(h[0] + n) + sin(h[1] + n))) / 12.0;
            const float t1 = (-cos(3.0 * h[0] - n) - cos(3.0 * h[1] - n) + 8.0 * cos(n) - 3.0 * (cos(h[0] + n) + cos(h[1] + n))) / 12.0;
            const float3 bentNormalL = float3(omega.x * t0, omega.y * t0, -t1);
            bent_normal += mul(rotMatrixToViewV, bentNormalL) * length(projNormalV);// Sliceプレーンへ投影された法線の長さで重み付け.
        }
    }
    
    visibility = visibility / float(slice_count);
    // bent normal正規化
    bent_normal = normalize(bent_normal);
    // World空間へ変換
    bent_normal = mul(cb_ngl_sceneview.cb_view_inv_mtx, float4(bent_normal, 0.0)).xyz;
        
    // 出力.
    RWTexGtaoBentNormal[dtid.xy] = float4(bent_normal, visibility);
}
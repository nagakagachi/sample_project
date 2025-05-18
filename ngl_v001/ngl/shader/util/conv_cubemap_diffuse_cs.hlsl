
#if 0
入力Cubemapを元にDiffuse Irradiance Cubemapに畳み込みを実行する.

Dispatchは x,y はplaneの解像度依存, z はCubemapの6面に対応する6を指定する

参考
https://learnopengl.com/PBR/IBL/Diffuse-irradiance

#endif

#include "../include/math_util.hlsli"

// 方位角(半球の周囲)のサンプル分割数.
#define CONV_DIFFUSE_AZIMUTH_RESOLUTION 256

TextureCube tex_cube;
SamplerState samp;

RWTexture2DArray<float4> uav_cubemap_as_array;

[numthreads(16,16,1)]
void main(
    uint3 dtid    : SV_DispatchThreadID,
    uint3 gtid    : SV_GroupThreadID,
    uint3 gid     : SV_GroupID,
    uint  gindex  : SV_GroupIndex
)
{
    const uint plane_index = gid.z;
    const uint2 texel_pos = dtid.xy;

    const float3 plane_axis_front[6] = {
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0), float3(0.0, -1.0, 0.0),
        float3(0.0, 0.0, 1.0), float3(0.0, 0.0, -1.0),
    };
    const float3 plane_axis_up[6] = {
        float3(0.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, -1.0), float3(0.0, 0.0, -1.0),
        float3(0.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
    };
    const float3 plane_axis_right[6] = {
        float3(0.0, 0.0, -1.0), float3(0.0, 0.0, 1.0),
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
    };
    const float3 front = plane_axis_front[plane_index];
    const float3 up = plane_axis_up[plane_index];
    const float3 right = plane_axis_right[plane_index];


    float width, height, count;
    uav_cubemap_as_array.GetDimensions(width, height, count);
    const float2 cubemap_uv = (float2(texel_pos) + float2(0.5, 0.5)) / float2(width, height);

    const float2 clip_space_pos_xy = cubemap_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float3 sample_normal = normalize( (up * clip_space_pos_xy.y) + (right * clip_space_pos_xy.x) + front);

    // sample_normal を中心として畳み込みをする.
    float4 tex_color = (float4)0;

    const float3 sample_right = normalize(cross(float3(0.0, 1.0, 0.0), sample_normal));
    const float3 sample_up = normalize(cross(sample_normal, sample_right));

    const int k_azimuth_sample_resolution = CONV_DIFFUSE_AZIMUTH_RESOLUTION;// 方位角のサンプル分割.
    const int k_polar_sample_resolution = (1.0/4.0)*k_azimuth_sample_resolution;// 天頂角のサンプル分割. 1/4の弧なので方位角の1/4.
    const float azimuth_delta = NGL_2PI / k_azimuth_sample_resolution;
    const float polar_delta = NGL_HALF_PI / k_polar_sample_resolution;
    // 方位角,天頂角で分割して積分.
    for(int phi_i = 0; phi_i < k_azimuth_sample_resolution; ++phi_i)
    {
        const float phi = azimuth_delta * phi_i;
        for(int theta_i = 0; theta_i < k_polar_sample_resolution; ++theta_i)
        {
            const float theta = polar_delta * theta_i;

            const float sin_theta = sin(theta);
            const float cos_theta = cos(theta);
            const float sin_phi = sin(phi);
            const float cos_phi = cos(phi);
            
            const float3 dir_ts = float3(sin_theta*cos_phi, sin_theta*sin_phi, cos_theta);

            const float3 dir_ws = sample_right*dir_ts.x + sample_up*dir_ts.y + sample_normal*dir_ts.z;
            //const float3 dir_ws = normalize((sample_right*dir_ts.x + sample_up*dir_ts.y)*0.2 + sample_normal*dir_ts.z);// テスト用にサンプル範囲を絞る.

            const float4 sample_color = tex_cube.SampleLevel(samp, dir_ws, 0);
            // 立体角の重みで積分.
            tex_color += sample_color * (cos_theta * sin_theta);
        }
    }
    tex_color = tex_color * NGL_PI * (1.0 / (k_azimuth_sample_resolution*k_polar_sample_resolution));
        

    // 書き込み位置. UAVアクセスでのレイアウトでCubemapの下面(-Y)がUV反転しているっぽい.
    const uint2 write_pos = (0.0 <= front.y)? texel_pos : uint2(width-1, height-1)-texel_pos;

    // 書き込み.
    uav_cubemap_as_array[uint3(write_pos, plane_index)] = tex_color;
}

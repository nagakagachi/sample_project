
#if 0
入力Cubemapを元にDiffuse Irradiance Cubemapに畳み込みを実行する.

Dispatchは x,y はplaneの解像度依存, z はCubemapの6面に対応する6を指定する

参考
https://learnopengl.com/PBR/IBL/Diffuse-irradiance

#endif

#include "../include/math_util.hlsli"

// 方位角(半球の周囲)のサンプル分割数.
// ソースCubemap解像度に対してサンプル数が少ないとアンダーサンプリングで高輝度ノイズが発生するため, Mipmapを利用した補正機能を用意してある.
#define CONV_DIFFUSE_AZIMUTH_RESOLUTION 256

struct CbConvCubemapDiffuse
{
	uint use_mip_to_prevent_undersampling;
};
ConstantBuffer<CbConvCubemapDiffuse> cb_conv_cubemap_diffuse;

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

    float3 front, up, right;
    GetCubemapPlaneAxis(plane_index, front, up, right);


    float input_width, input_height;
    tex_cube.GetDimensions(input_width, input_height);

    float output_width, output_height, output_plane_count;
    uav_cubemap_as_array.GetDimensions(output_width, output_height, output_plane_count);
    const float2 cubemap_uv = (float2(texel_pos) + float2(0.5, 0.5)) / float2(output_width, output_height);

    const float2 clip_space_pos_xy = cubemap_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float3 sample_normal = normalize( (up * clip_space_pos_xy.y) + (right * clip_space_pos_xy.x) + front);



    const int k_azimuth_sample_resolution = CONV_DIFFUSE_AZIMUTH_RESOLUTION;// 方位角のサンプル分割.
    const int k_polar_sample_resolution = (1.0/4.0)*k_azimuth_sample_resolution;// 天頂角のサンプル分割. 1/4の弧なので方位角の1/4.
    const float azimuth_delta = NGL_2PI / k_azimuth_sample_resolution;// 方位角の1sample毎のDelta.
    const float polar_delta = NGL_HALF_PI / k_polar_sample_resolution;// 天頂角の1sample毎のDelta.

	// 高周波ノイズ対策としてCubemapのオフセットしたMipを参照する.
	float mip_bias_for_undersampling_compensation = 0.0;
	if(0!=cb_conv_cubemap_diffuse.use_mip_to_prevent_undersampling)
	{
		// polar_delta は90度範囲での大凡のサンプル分解能なので, それを元にソースのCubemapの解像度をアンダーサンプリングしないMipオフセットを計算する.
		const float src_polar_delta = 1.0/input_width;
		const float delta_ratio = polar_delta / src_polar_delta;
		mip_bias_for_undersampling_compensation = max(log2(delta_ratio), 0) + 1.0;// deltaの比から求めたMipオフセットへバイアスを加算してノイズ抑制.
	}

    // sample_normal を中心として畳み込みをする.
    float4 tex_color = (float4)0;

    const float3 sample_right = normalize(cross(float3(0.0, 1.0, 0.0), sample_normal));
    const float3 sample_up = normalize(cross(sample_normal, sample_right));

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

            const float4 sample_color = tex_cube.SampleLevel(samp, dir_ws, mip_bias_for_undersampling_compensation);
            // 立体角の重みで積分.
            tex_color += sample_color * (cos_theta * sin_theta);
        }
    }
    tex_color = tex_color * NGL_PI * (1.0 / (k_azimuth_sample_resolution*k_polar_sample_resolution));
        
    const uint2 write_pos = texel_pos;
    // 書き込み.
    uav_cubemap_as_array[uint3(write_pos, plane_index)] = tex_color;
}


#if 0

方向差分による近似的なCubemap 1/2 のMip生成.

Dispatchは x,y はplaneの解像度依存, z はCubemapの6面に対応する6を指定する
#endif

#include "../include/math_util.hlsli"

TextureCube tex_cube_mip_parent;
SamplerState samp;

RWTexture2DArray<float4> uav_cubemap_mip_as_array;

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

    float width, height, count;
    uav_cubemap_mip_as_array.GetDimensions(width, height, count);
    const float2 cubemap_uv = (float2(texel_pos) + float2(0.5, 0.5)) / float2(width, height);

    const float2 clip_space_pos_xy = cubemap_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float3 sample_normal = normalize( (up * clip_space_pos_xy.y) + (right * clip_space_pos_xy.x) + front);

    // sample_normal を中心として畳み込みをする.
    float4 tex_color = (float4)0;

    const float3 sample_right = normalize(cross(float3(0.0, 1.0, 0.0), sample_normal));
    const float3 sample_up = normalize(cross(sample_normal, sample_right));

    // 方位角,天頂角で分割して積分.
    const int k_sample_count = 2;
    for(int ti = 0; ti < k_sample_count; ++ti)
    {
        const float signed_ti = (ti + 0.5) - (k_sample_count * 0.5);
        for(int tj = 0; tj < k_sample_count; ++tj)
        {
            const float signed_tj = (tj + 0.5) - (k_sample_count * 0.5);

            const float2 offset_ts = float2(signed_ti, signed_tj) / width;

            const float3 dir_ts = normalize(float3(offset_ts, 1.0));
            const float3 dir_ws = sample_right*dir_ts.x + sample_up*dir_ts.y + sample_normal*dir_ts.z;

            const float4 sample_color = tex_cube_mip_parent.SampleLevel(samp, dir_ws, 0);
            // ボックスフィルタ相当.
            tex_color += sample_color;
        }
    }
    tex_color *= (1.0 / (k_sample_count * k_sample_count));
        
    // 書き込み.
    uav_cubemap_mip_as_array[uint3(texel_pos, plane_index)] = tex_color;
}

#if 0

vsp_probe_sh_update_cs.hlsl

ファイル説明:
 Visibility Surface Probe の OctMap atlas から SkyVisibility + Radiance の L1 SH を作り、
 cascaded dense IrradianceVolume へ global cell index 直結で書き込む。
 coeff order:
   0 = Y00
   1 = Y1_{-1}(y)
   2 = Y1_0(z)
   3 = Y1_{+1}(x)
 packed RGBA:
   R = SkyVisibility coeff
   G = Radiance R coeff
   B = Radiance G coeff
   A = Radiance B coeff

#endif

#include "../instant_rdv_util.hlsli"

[numthreads(PROBE_UPDATE_THREAD_GROUP_SIZE, 1, 1)]
void main_cs(
    uint3 dtid : SV_DispatchThreadID,
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint gindex : SV_GroupIndex)
{
    const uint active_probe_count =
        VspActiveProbeListCurr[VspActiveProbeCurrentCounterSlot()];
    if(dtid.x >= active_probe_count)
    {
        return;
    }

    const uint probe_index =
        VspActiveProbeListCurr[VspActiveProbeListAddress(dtid.x)];
    if(probe_index >= cb_instant_rdv.vsp_probe_pool_size)
    {
        return;
    }

    const VspProbePoolData probe_pool_data = VspProbePoolBuffer[probe_index];
    if(probe_pool_data.owner_cell_index == k_vsp_invalid_probe_index)
    {
        return;
    }

    float4 packed_sh_coeff0 = 0.0.xxxx;
    float4 packed_sh_coeff1 = 0.0.xxxx;
    float4 packed_sh_coeff2 = 0.0.xxxx;
    float4 packed_sh_coeff3 = 0.0.xxxx;

    [unroll]
    for(int oy = 0; oy < k_vsp_probe_octmap_width; ++oy)
    {
        [unroll]
        for(int ox = 0; ox < k_vsp_probe_octmap_width; ++ox)
        {
            const uint2 atlas_texel_pos = VspProbeAtlasTexelCoord(probe_index, uint2(ox, oy));
            const float4 vsp_probe_value = VspProbeAtlasTex.Load(int3(atlas_texel_pos, 0));
            const float4 packed_sample = float4(vsp_probe_value.a, vsp_probe_value.rgb);

            const float2 oct_uv = (float2(float(ox), float(oy)) + 0.5.xx) / float(k_vsp_probe_octmap_width);
            const float3 dir_ws = OctDecode(oct_uv);
            const float4 sh_basis = EvaluateL1ShBasis(dir_ws);

            packed_sh_coeff0 += packed_sample * sh_basis.x;
            packed_sh_coeff1 += packed_sample * sh_basis.y;
            packed_sh_coeff2 += packed_sample * sh_basis.z;
            packed_sh_coeff3 += packed_sample * sh_basis.w;
        }
    }

    const float texel_solid_angle = (4.0 * 3.14159265359) / float(k_vsp_probe_octmap_width * k_vsp_probe_octmap_width);
    // Probe atlas はRT resolve用の中間履歴で、最終シェーディング用SHはowner cellのdense volumeへ集約する。
    // RGBは書き込み時にLambertのclamped-cosineを畳み込み、サンプリング側の評価を軽くする。
    const uint global_cell_index = probe_pool_data.owner_cell_index;
    packed_sh_coeff0 *= texel_solid_angle;
    packed_sh_coeff1 *= texel_solid_angle;
    packed_sh_coeff2 *= texel_solid_angle;
    packed_sh_coeff3 *= texel_solid_angle;
    const float4 sky_visibility_sh = float4(
        packed_sh_coeff0.r, packed_sh_coeff1.r, packed_sh_coeff2.r, packed_sh_coeff3.r);
    const float4 irradiance_sh_r = ConvolveL1ShByClampedCosine(float4(
        packed_sh_coeff0.g, packed_sh_coeff1.g, packed_sh_coeff2.g, packed_sh_coeff3.g));
    const float4 irradiance_sh_g = ConvolveL1ShByClampedCosine(float4(
        packed_sh_coeff0.b, packed_sh_coeff1.b, packed_sh_coeff2.b, packed_sh_coeff3.b));
    const float4 irradiance_sh_b = ConvolveL1ShByClampedCosine(float4(
        packed_sh_coeff0.a, packed_sh_coeff1.a, packed_sh_coeff2.a, packed_sh_coeff3.a));
    VspIrradianceVolumeStoreSignals(
        global_cell_index,
        sky_visibility_sh,
        irradiance_sh_r,
        irradiance_sh_g,
        irradiance_sh_b);
}

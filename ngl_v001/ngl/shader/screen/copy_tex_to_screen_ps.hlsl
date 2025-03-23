#if 0

入力テクスチャをそのまま出力.

#endif



struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};

Texture2D tex;
SamplerState samp;

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{
    return tex.SampleLevel(samp, input.uv, 0);
}

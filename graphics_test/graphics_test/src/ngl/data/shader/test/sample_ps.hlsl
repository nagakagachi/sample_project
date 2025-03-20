

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};


cbuffer CbSamplePs
{
	float4 cb_param0;
};


Texture2D		TexPs0;
Texture2D		TexPs1;
SamplerState	SmpPs;

float4 main_ps(VS_OUTPUT input) : SV_TARGET
{

	float4 tex0 = TexPs0.SampleLevel(SmpPs, input.uv, 0);

	const float2 sub_tex_size = float2(0.4, 0.4);
	float4 tex1 = TexPs1.SampleLevel(SmpPs, input.uv / sub_tex_size, 0);

	float4 col = tex0;

	if (input.uv.x < sub_tex_size.x && input.uv.y < sub_tex_size.y)
	{
		col = tex1;
	}

	// 無理やりVSステージでのConstantBuffeを使うためのテスト.
	if (1e5 == cb_param0.x)
	{
		col.w = 1.0;
	}

	return col;
}
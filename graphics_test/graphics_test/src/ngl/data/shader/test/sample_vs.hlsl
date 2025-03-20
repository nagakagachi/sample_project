



struct VS_INPUT
{
	float4 pos	:	POSITION;
	float2 uv	:	TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 pos	:	SV_POSITION;
	float2 uv	:	TEXCOORD0;
};


cbuffer CbSampleVs
{
	float4 cb_param_vs0;
};


VS_OUTPUT main_vs(VS_INPUT input)
{
	VS_OUTPUT output = (VS_OUTPUT)0;

	output.pos = input.pos;
	output.uv = input.uv;

	// 無理やりVSステージでのConstantBuffeを使うためのテスト.
	if (1e5 == cb_param_vs0.x)
	{
		output.uv = input.uv;
	}

	return output;
}
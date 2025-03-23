/*
	asyn_task_test_cs.hlsl
	
 */




RWTexture2D<float4>	rwtex_out;


[numthreads(8, 8, 1)]
void main_cs(
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid : SV_GroupThreadID,
	uint3 gid : SV_GroupID,
	uint gindex : SV_GroupIndex
)
{
	uint out_w, out_h;
	rwtex_out.GetDimensions(out_w, out_h);

	if(out_w <= dtid.x || out_h <= dtid.y)
		return;

	// 適当に重そうな処理.
	const int k_loop_num = 100;
	float4 v = (float4)0;
	for(int i = 0; i < k_loop_num; ++i)
	{
		const float2 r = dtid.xy / (1 + i);
		v += sin(dot(r, float2(125.1, 79.1)));
	}
	v /= k_loop_num;
	
	rwtex_out[dtid.xy] = v;
}
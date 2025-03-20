
// register指定(バインディング) 記述の注意.
// DXRのRayTracing ShaderはStateObject生成時にShaderとRootSignatureのバインディングを完全にチェックするため
// register指定を省略するとチェックに引っかかりStateObject生成に失敗する.


#if 0
// Comment

	Global Root Sig のレイアウト. システム化を簡易にするために固定インデックスから固定数を固定用途. 完全に使っていなくても不正なアクセスさえしなければOK.
		0 RootDesc t65535(AS)
		1 DescTable b0 - b15
		2 DescTable t0 - b15
		3 DescTable s0 - b15
		4 DescTable u0 - b15


	検討中
	Local Root Sig のレイアウト.
	わかりやすくするために 100 以降のregister indexにする. GlobalとLocal間ではオーバラップはエラー. Local間は独立しているので同じindexを使っても構わない とおもう.
	ここからShaderTableのレコードのリソース部はDescriptor4つ分になるか
	またHeapサイズの制限からLocal側はSampler使用不可とする. Global側の共有Sampler枠でやりくりする予定.
	UAVもとりあえず無しで.
		0 DescTable b1000 - b1015
		1 DescTable t1000 - b1015	// tにはBLASの頂点バッファなども欲しいため48個くらいまで用意したほうがいいかも?



#endif
//

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )

#include "include/math_util.hlsli"

// SceneView定数バッファ構造定義.
#include "include/scene_view_struct.hlsli"
ConstantBuffer<SceneViewInfo> ngl_cb_sceneview : register(b0);


// Global Srv.
// システム供給のASはGlobalRootで衝突しないと思われるレジスタで供給.
RaytracingAccelerationStructure	rt_as : register(t65535);


// Global Srv.
RWTexture2D<float4>				out_uav : register(u0);



struct Payload
{
	float4 color;
};


[shader("raygeneration")]
void rayGen()
{
	uint3 launch_index = DispatchRaysIndex();
	uint3 launch_dim = DispatchRaysDimensions();

	const float2 screen_pos_f = float2(launch_index.xy) + float2(0.5, 0.5);// ピクセル中心への半ピクセルオフセット考慮.
	const float2 screen_size_f = float2(launch_dim.xy);
	const float2 screen_uv = (screen_pos_f / screen_size_f);

	float3 ray_dir;
	{
		float3 to_pixel_ray_vs = CalcViewSpaceRay(screen_uv, ngl_cb_sceneview.cb_proj_mtx);
		ray_dir = mul(ngl_cb_sceneview.cb_view_inv_mtx, float4(to_pixel_ray_vs, 0.0));
	}

	RayDesc ray;
	ray.Origin = float3(ngl_cb_sceneview.cb_view_inv_mtx._m03, ngl_cb_sceneview.cb_view_inv_mtx._m13, ngl_cb_sceneview.cb_view_inv_mtx._m23);// View逆行列からRay始点取得.
	ray.Direction = ray_dir;
	ray.TMin = 0.0;
	ray.TMax = 1e38;

	Payload payload;
	const int ray_flag = 0;
	// HItGroupIndex計算時に加算される値. ShadowやAO等のPass毎のシェーダを切り替える際のインデックスに使うなど.
	const int ray_contribution_to_hitgroup = 0;
	// BLAS中のSubGeometryIndexに乗算される値. 結果はHitGroupIndex計算時に加算される.
	// BLAS中のSubGeometryIndexがそれぞれ別のHitGroupを利用する場合は1等, BLAS中のすべてが同じHitGroupなら0を指定するなどが考えられる.
	// 1に設定する場合はShaderTable構築時にBLAS内Geom分考慮したEntry登録が必要.
	const int multiplier_for_subgeometry_index = 1;
	const int miss_shader_index = (launch_index.x/20+launch_index.y/20)&1;//0;// ２つMissShaderが登録されている体で適当に切り替え.
	TraceRay(rt_as, ray_flag, 0xff, ray_contribution_to_hitgroup, multiplier_for_subgeometry_index, miss_shader_index, ray, payload );

	float3 col = payload.color.xyz;

	out_uav[launch_index.xy] = float4(col, 0.0);
}


[shader("miss")]
void miss(inout Payload payload)
{
	payload.color = float4(0.0, 0.0, 0.0, 0.0);
}

// 2つ目のMissShaderテスト. TraceRayの引数でどのMissShaderを利用するか直接的に指定する.
[shader("miss")]
void miss2(inout Payload payload)
{
	payload.color = float4(0.0, 0.8, 0.8, 0.0);
}


// ------------------------------------------------------
// Hitgroup Local Root Sig.
	// HitGroup毎のジオメトリデータをLocalRootSigに設定している.
	// Tir 頂点位置.
	Buffer<float3>	srv_prim_vertex_pos	: register(t1000);
	// Tri Index
	Buffer<uint>	srv_prim_index		: register(t1001);
// ------------------------------------------------------

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	float ray_t = RayTCurrent();
	uint instanceId = InstanceID();
	uint geomIndex = GeometryIndex();

#if 1
	// ジオメトリ情報収集テスト
	uint tri_index_head = PrimitiveIndex() * 3;
	const uint3 tri_index = { srv_prim_index[tri_index_head + 0], srv_prim_index[tri_index_head + 1], srv_prim_index[tri_index_head + 2] };

	// Retrieve vertices for the hit triangle.
	float3 tri_vetex_pos[3] = {
		srv_prim_vertex_pos[tri_index[0]],
		srv_prim_vertex_pos[tri_index[1]],
		srv_prim_vertex_pos[tri_index[2]] };
	// 頂点データに無いのでここで法線計算.
	float3 tri_n_ls = normalize(cross(tri_vetex_pos[1] - tri_vetex_pos[0], tri_vetex_pos[2] - tri_vetex_pos[0]));

	// ワールド空間法線に変換.
	// 不均等スケールの場合に対応するため余因子行列変換(この場で計算せずに構造化バッファにすることを検討)
	float3 tri_n_ws = mul( Matrix3x3_Cofactor((float3x3)ObjectToWorld3x4()), tri_n_ls);
	//// builtin ObjectToWorld3x4() をそのまま法線変換に使用すると不均等スケールの場合に正しくない.
	////float3 tri_n_ws = mul((float3x3)ObjectToWorld3x4(), tri_n_ls);
	
	// ワールド法線の可視化.
	payload.color = float4((normalize(tri_n_ws)*0.5+0.5), 0.0);
#else

	// デバッグ用に距離で色変化.
	float distance_color = frac(ray_t / 20.0);
	payload.color = float4(distance_color, distance_color, distance_color, 0.0);
#endif
}

// 2つ目のhitgroupテスト.
[shader("closesthit")]
void closestHit2(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	float ray_t = RayTCurrent();
	uint instanceId = InstanceID();
	uint geomIndex = GeometryIndex();

#if 0
	// ジオメトリ情報収集テスト
	uint tri_index_head = PrimitiveIndex() * 3;
	const uint3 tri_index = { srv_prim_index[tri_index_head + 0], srv_prim_index[tri_index_head + 1], srv_prim_index[tri_index_head + 2] };

	// Retrieve vertices for the hit triangle.
	float3 tri_vetex_pos[3] = {
		srv_prim_vertex_pos[tri_index[0]],
		srv_prim_vertex_pos[tri_index[1]],
		srv_prim_vertex_pos[tri_index[2]] };
	// 頂点データに無いのでここで法線計算.
	float3 tri_n_ls = normalize(cross(tri_vetex_pos[1] - tri_vetex_pos[0], tri_vetex_pos[2] - tri_vetex_pos[0]));
	// builtin ObjectToWorld3x4() でWorld変換可能. Object空間のRayDirなどもBuildinがある.
	float3 tri_n_ws = mul((float3x3)ObjectToWorld3x4(), tri_n_ls);

	// ワールド法線の可視化.
	payload.color = float4(abs(tri_n_ws), 0.0);
#else
	// デバッグ用に重心座標の可視化テスト.
	//float3 bary = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
	//payload.color = float4(bary, 0.0);

	// デバッグ用に距離で色変化.
	float distance_color = frac(ray_t / 20.0);
	payload.color = float4(distance_color, distance_color, distance_color, 0.0);
#endif
}
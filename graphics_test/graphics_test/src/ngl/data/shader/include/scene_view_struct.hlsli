#ifndef NGL_SHADER_SCENE_VIEW_STRUCT_H
#define NGL_SHADER_SCENE_VIEW_STRUCT_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )


struct SceneViewInfo
{
	float3x4 cb_view_mtx;
	float3x4 cb_view_inv_mtx;
	float4x4 cb_proj_mtx;
	float4x4 cb_proj_inv_mtx;

	// 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化.
	//	view_z = cb_ndc_z_to_view_z_coef.x / ( ndc_z * cb_ndc_z_to_view_z_coef.y + cb_ndc_z_to_view_z_coef.z )
	//
	//		cb_ndc_z_to_view_z_coef = 
	//			Standard RH: (-far_z * near_z, near_z - far_z, far_z, 0.0)
	//			Standard LH: ( far_z * near_z, near_z - far_z, far_z, 0.0)
	//			Reverse RH: (-far_z * near_z, far_z - near_z, near_z, 0.0)
	//			Reverse LH: ( far_z * near_z, far_z - near_z, near_z, 0.0)
	//			Infinite Far Reverse RH: (-near_z, 1.0, 0.0, 0.0)
	//			Infinite Far Reverse RH: ( near_z, 1.0, 0.0, 0.0)
	float4	cb_ndc_z_to_view_z_coef;

	float	cb_time_sec;
};

// 十分なサイズ指定.
#define k_directional_shadow_cascade_cb_max 8
// Directional Cascade Shadow Sampling用 定数バッファ構造定義.
struct SceneDirectionalShadowSampleInfo
{
	float3x4 cb_shadow_view_mtx[k_directional_shadow_cascade_cb_max];
	float3x4 cb_shadow_view_inv_mtx[k_directional_shadow_cascade_cb_max];
	float4x4 cb_shadow_proj_mtx[k_directional_shadow_cascade_cb_max];
	float4x4 cb_shadow_proj_inv_mtx[k_directional_shadow_cascade_cb_max];

	float4 cb_cascade_tile_uvoffset_uvscale[k_directional_shadow_cascade_cb_max];
	
	// // 各Cascadeの遠方側境界のView距離. 格納はアライメント対策で4要素ずつ. アライメント対策で4単位.
	// シーケンシャルアクセスする場合は インデックスiについて cb_cascade_far_distance4[i/4][i%4] という記述で可能.
	float4 cb_cascade_far_distance4[k_directional_shadow_cascade_cb_max/4];
	
	int cb_valid_cascade_count;
};


#endif // NGL_SHADER_SCENE_VIEW_STRUCT_H
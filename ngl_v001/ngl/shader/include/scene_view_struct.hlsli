/*
    scene_view_struct.hlsli
    SceneView定数バッファ構造定義.
*/

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

    // 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化. xは平行投影もサポートするために利用.
    //	for calc_view_z_from_ndc_z(ndc_z, cb_ndc_z_to_view_z_coef)
	float4	cb_ndc_z_to_view_z_coef;

	float	cb_time_sec;
};

// PerspectiveとOrthogonalの両方に同じ係数同じ計算で対応するため, 分子の乗算と加算, 分母の乗算と加算のパラメータをそれぞれ指定.
//	view_z = (ndc_z * cb_ndc_z_to_view_z_coef.x + cb_ndc_z_to_view_z_coef.y) / ( ndc_z * cb_ndc_z_to_view_z_coef.z + cb_ndc_z_to_view_z_coef.w )
//		ndc_z_to_view_z_coef = 
//			Standard RH: (0.0, -far_z * near_z, near_z - far_z, far_z)
//			Standard LH: (0.0, far_z * near_z, near_z - far_z, far_z)
//			Reverse RH: (0.0, -far_z * near_z, far_z - near_z, near_z)
//			Reverse LH: (0.0, far_z * near_z, far_z - near_z, near_z)
//			Infinite Far Reverse RH: (0.0, -near_z, 1.0, 0.0)
//			Infinite Far Reverse RH: (0.0, near_z, 1.0, 0.0)
float calc_view_z_from_ndc_z(float ndc_z, float4 ndc_z_to_view_z_coef)
{
    return (ndc_z * ndc_z_to_view_z_coef.x + ndc_z_to_view_z_coef.y) / ( ndc_z * ndc_z_to_view_z_coef.z + ndc_z_to_view_z_coef.w );
}


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
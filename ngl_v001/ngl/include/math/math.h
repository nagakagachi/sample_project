#pragma once

#include <cmath>
#include <memory>

#include <assert.h>

#include "detail/math_vector.h"
#include "detail/math_matrix.h"
#include "detail/math_util.h"
#include "detail/math_curve.h"

namespace ngl
{
	namespace math
	{
		constexpr float		k_pi_f = 3.141592653589793f;
		constexpr double	k_pi_d = 3.141592653589793;
		static constexpr float Deg2Rad(float degree)
		{
			return degree * k_pi_f / 180.0f;
		}
		static constexpr float Rad2Deg(float radian)
		{
			return  radian * 180.0f / k_pi_f;
		}



		// -------------------------------------------------------------------------------------------
		// Matrix Vector 演算系
		
		// Mat * Vector(column)
		inline constexpr Vec2 operator*(const Mat22& m, const Vec2& v)
		{
			return Vec2(Vec2::Dot(m.r0, v), Vec2::Dot(m.r1, v));
		}
		// Vector(row) * Mat
		inline constexpr Vec2 operator*(Vec2& v, const Mat22& m)
		{
			return m.r0 * v.x + m.r1 * v.y;
		}

		// Mat * Vector(column)
		inline constexpr Vec3 operator*(const Mat33& m, const Vec3& v)
		{
			return Vec3(Vec3::Dot(m.r0, v), Vec3::Dot(m.r1, v), Vec3::Dot(m.r2, v));
		}
		// Vector(row) * Mat
		inline constexpr Vec3 operator*(Vec3& v, const Mat33& m)
		{
			return m.r0 * v.x + m.r1 * v.y + m.r2 * v.z;
		}

		// Mat * Vector(column)
		inline constexpr Vec4 operator*(const Mat44& m, const Vec4& v)
		{
			return Vec4(Vec4::Dot(m.r0, v), Vec4::Dot(m.r1, v), Vec4::Dot(m.r2, v), Vec4::Dot(m.r3, v));
		}
		// Vector(row) * Mat
		inline constexpr Vec4 operator*(Vec4& v, const Mat44& m)
		{
			return m.r0 * v.x + m.r1 * v.y + m.r2 * v.z + m.r3 * v.w;
		}

		// Mat * Vector(column)
		inline constexpr Vec3 operator*(const Mat34& m, const Vec3& v)
		{
			const Vec4 w1(v, 1);
			return Vec3(Vec4::Dot(m.r0, w1), Vec4::Dot(m.r1, w1), Vec4::Dot(m.r2, w1));
		}




		// ------------------------------------------------------------------------------------------------
		// Utility
		static constexpr bool k_defalut_right_hand_mode = false;
		
		// View Matrix.
		Mat34 CalcViewMatrix(const Vec3& camera_location, const Vec3& forward, const Vec3& up, bool is_right_hand = k_defalut_right_hand_mode);
		

        // 4係数でNDC空間ZからView空間Zを復元する. Projection毎に適切に4係数を求めることで共通処理でView空間Zを復元する.
        //  復元
        //	    view_z = (ndc_z * cb_ndc_z_to_view_z_coef.x + cb_ndc_z_to_view_z_coef.y) / ( ndc_z * cb_ndc_z_to_view_z_coef.z + cb_ndc_z_to_view_z_coef.w )
        //
        //  係数
        //		ndc_z_to_view_z_coef = CalcViewDepthReconstructCoefFromProjectionMatrix(projection_matrix)
        constexpr float calc_view_z_from_ndc_z(float ndc_z, Vec4 ndc_z_to_view_z_coef)
        {
            return (ndc_z * ndc_z_to_view_z_coef.x + ndc_z_to_view_z_coef.y) / ( ndc_z * ndc_z_to_view_z_coef.z + ndc_z_to_view_z_coef.w );
        }
        
        // Projection Matrix から View空間Z復元係数を計算する.
        constexpr Vec4 CalcViewDepthReconstructCoefFromProjectionMatrix(const Mat44& proj_mat)
        {
            // 元の式: ndc_z = (view_z * proj_mat.m22 + proj_mat.m23) / (view_z * proj_mat.m32 + proj_mat.m33)
            // 
            // 1. 両辺に分母を乗算:
            //    ndc_z * (view_z * proj_mat.m32 + proj_mat.m33) = view_z * proj_mat.m22 + proj_mat.m23
            // 
            // 2. 左辺を展開:
            //    ndc_z * view_z * proj_mat.m32 + ndc_z * proj_mat.m33 = view_z * proj_mat.m22 + proj_mat.m23
            // 
            // 3. view_z項を左辺に集約:
            //    ndc_z * view_z * proj_mat.m32 - view_z * proj_mat.m22 = proj_mat.m23 - ndc_z * proj_mat.m33
            // 
            // 4. view_zで因数分解:
            //    view_z * (ndc_z * proj_mat.m32 - proj_mat.m22) = proj_mat.m23 - ndc_z * proj_mat.m33
            // 
            // 5. view_zについて解く:
            //    view_z = (proj_mat.m23 - ndc_z * proj_mat.m33) / (ndc_z * proj_mat.m32 - proj_mat.m22)
            //    view_z = (-ndc_z * proj_mat.m33 + proj_mat.m23) / (ndc_z * proj_mat.m32 - proj_mat.m22)
            // 
            // 6. 4係数形式 view_z = (ndc_z * a + b) / (ndc_z * c + d) に変換:
            //    a = -proj_mat.m33, b = proj_mat.m23, c = proj_mat.m32, d = -proj_mat.m22

            Vec4 coef(-proj_mat.m[3][3], proj_mat.m[2][3], proj_mat.m[3][2], -proj_mat.m[2][2]);
            return coef;
        }
        
		// Standard Perspective Projection Matrix (default:LeftHand).
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcStandardPerspectiveMatrix(float fov_y_radian, float aspect_ratio, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		
		// Reverse Perspective Projection Matrix.
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcReversePerspectiveMatrix(float fov_y_radian, float aspect_ratio, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		
		// InfiniteFar and Reverse Z Perspective Projection Matrix.
		//	fov_y_radian : full angle of Vertical FOV.
		//	Z-> near:1, far:0
		//	https://thxforthefish.com/posts/reverse_z/
		Mat44 CalcReverseInfiniteFarPerspectiveMatrix(float fov_y_radian, float aspect_ratio, float near_z, bool is_right_hand = k_defalut_right_hand_mode);
		
		// 標準平行投影.
		Mat44 CalcStandardOrthographicMatrix(float left, float right, float bottom, float top, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		// 標準平行投影.
		Mat44 CalcStandardOrthographicSymmetricMatrix(float width, float height, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
        
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicMatrix(float left, float right, float bottom, float top, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicSymmetricMatrix(float width, float height, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);



        /* CalcViewDepthReconstructCoefFromProjectionMatrix で汎用処理化したので除去予定.

		// for calc_view_z_from_ndc_z()
		inline constexpr Vec4 CalcViewDepthReconstructCoefForReverseInfiniteFarPerspective(float near_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(0.0, sign * near_z, 1.0f, 0.0);
			return coef;
		}
		// for calc_view_z_from_ndc_z()
		inline constexpr Vec4 CalcViewDepthReconstructCoefForStandardPerspective(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(0.0, sign * far_z * near_z, near_z - far_z, far_z);
			return coef;
		}
		// for calc_view_z_from_ndc_z()
		inline constexpr Vec4 CalcViewDepthReconstructCoefForReversePerspective(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(0.0, sign * far_z * near_z, far_z - near_z, near_z);
			return coef;
		}
        // for calc_view_z_from_ndc_z()
        inline constexpr Vec4 CalcViewDepthReconstructCoefForStandardOrthographic(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
        {
            // view_z * z_sign * (far_z - near_z) + -near_z * (far_z - near_z) = ndc_z
            // 上の式から view_z を求めると以下になる:
            // view_z = (ndc_z + near_z * (far_z - near_z)) / (z_sign * (far_z - near_z))
            // 
            // 4係数形式 view_z = (ndc_z * a + b) / (ndc_z * c + d) に変換すると:
            // a = 1.0, b = near_z * (far_z - near_z), c = 0.0, d = z_sign * (far_z - near_z)
            
            const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;
            const float far_near_diff = far_z - near_z;
            Vec4 coef(1.0f, near_z * far_near_diff, 0.0f, z_sign * far_near_diff);
            return coef;
        }
        // for calc_view_z_from_ndc_z()
        inline constexpr Vec4 CalcViewDepthReconstructCoefForReverseOrthographic(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
        {
            // Reverse平行投影では range_term = 1.0f / (near_z - far_z) で符号が反転
            // view_z * z_sign * (near_z - far_z) + -far_z * (near_z - far_z) = ndc_z
            // 上の式から view_z を求めると以下になる:
            // view_z = (ndc_z + far_z * (near_z - far_z)) / (z_sign * (near_z - far_z))
            // 
            // 4係数形式 view_z = (ndc_z * a + b) / (ndc_z * c + d) に変換すると:
            // a = 1.0, b = far_z * (near_z - far_z), c = 0.0, d = z_sign * (near_z - far_z)
            
            const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;
            const float near_far_diff = near_z - far_z; // Reverseでは near_z - far_z
            Vec4 coef(1.0f, far_z * near_far_diff, 0.0f, z_sign * near_far_diff);
            return coef;
        }
        // for calc_view_z_from_ndc_z()
        inline constexpr Vec4 CalcViewDepthReconstructCoefForCalcReverseOrthographicSymmetric(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
        {
            return CalcViewDepthReconstructCoefForReverseOrthographic(near_z, far_z, is_right_hand);
        }
        */



		// ------------------------------------------------------------------------------------------------
		inline void funcAA()
		{
			static constexpr auto  v3_0 = Vec3();
			static constexpr auto  v4_0 = Vec4();

			static constexpr auto  v3_1 = Vec3(1.0f);
			static constexpr auto  v4_1 = Vec4(1.0f);

			static constexpr auto  v3_2 = Vec3(0.0f, 1.0f, 2.0f);
			static constexpr auto  v4_2 = Vec4(0.0f, 1.0f, 2.0f, 3.0f);

			static constexpr Vec2  v2_ux = -Vec2::UnitX();
			static constexpr Vec3  v3_ux = -Vec3::UnitX();
			static constexpr Vec4  v4_ux = -Vec4::UnitX();

			auto v2_ux_ = v2_ux;
			auto v3_ux_ = v3_ux;
			auto v4_ux_ = v4_ux;


			constexpr auto v4_negative = -v4_2;


			ngl::math::Vec3 v3_3(0, 1, 2);
			v3_3 += v3_2;

			ngl::math::Vec4 v4_3(0, 1, 2, 3);
			v4_3 += v4_2;

			v4_3 *= v4_2;
			v4_3 /= v4_1;


			constexpr auto v4_4 = v4_1 + v4_2 - v4_2;

			constexpr auto v_mul0 = ngl::math::Vec4(1.0f) * 2.0f;
			constexpr auto v_mul1 = 0.5f * ngl::math::Vec4(1.0f);

			constexpr auto v_div0 = ngl::math::Vec2(1.0f) / 2.0f;
			constexpr auto v_div1 = 5.0f / ngl::math::Vec4(1.0f, 2, 3, 4);



			ngl::math::Vec3 v3_4(1, 2, 3);
			v3_4 /= ngl::math::Vec3(3, 2, 1) / ngl::math::Vec3(2);

			constexpr auto v3_5 = ngl::math::Vec3(2) / ngl::math::Vec3(4);
			constexpr auto v3_6 = ngl::math::Vec3(2) * ngl::math::Vec3(4);

			constexpr auto v3_dot0 = ngl::math::Vec3::Dot(ngl::math::Vec3::UnitX(), ngl::math::Vec3(2));



			constexpr auto v3_cross_xy = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitX(), ngl::math::Vec3::UnitY());
			constexpr auto v3_cross_yz = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitY(), ngl::math::Vec3::UnitZ());
			constexpr auto v3_cross_zx = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitZ(), ngl::math::Vec3::UnitX());

			constexpr auto v3_cross_yx = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitY(), ngl::math::Vec3::UnitX());
			constexpr auto v3_cross_zy = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitZ(), ngl::math::Vec3::UnitY());
			constexpr auto v3_cross_xz = ngl::math::Vec3::Cross(ngl::math::Vec3::UnitX(), ngl::math::Vec3::UnitZ());

			const auto v3_cross_yx_len = ngl::math::Vec3::Length(v3_cross_yx);
			const auto v3_dot0_len = v3_6.Length();


            // テスト.
            struct ViewInfo
            {
                float camera_fov_y = Deg2Rad(60.0f); // radian
                float aspect_ratio = 16.0f / 9.0f;
                float near_z = 0.1f;
                float far_z = 1000.0f;
            };
            ViewInfo view_info{};
            const math::Mat44 proj_mat_CalcReverseInfiniteFarPerspectiveMatrix = math::CalcReverseInfiniteFarPerspectiveMatrix(view_info.camera_fov_y, view_info.aspect_ratio, view_info.near_z);
            const math::Vec4 ndc_z_to_view_z_coef_CalcReverseInfiniteFarPerspectiveMatrix_general = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat_CalcReverseInfiniteFarPerspectiveMatrix);
            const math::Vec4 pos_proj_RevInfPersp = proj_mat_CalcReverseInfiniteFarPerspectiveMatrix * math::Vec4(0.0f, 0.0f, 500.0f, 1.0f);
            const math::Vec4 pos_ndc_RevInfPersp = pos_proj_RevInfPersp / pos_proj_RevInfPersp.w;
            const float view_z_RevInfPersp_1 = math::calc_view_z_from_ndc_z(pos_ndc_RevInfPersp.z, ndc_z_to_view_z_coef_CalcReverseInfiniteFarPerspectiveMatrix_general);

            const math::Mat44 proj_mat_CalcReversePerspectiveMatrix = math::CalcReversePerspectiveMatrix(view_info.camera_fov_y, view_info.aspect_ratio, view_info.near_z, view_info.far_z);
            const math::Vec4 ndc_z_to_view_z_coef_CalcReversePerspectiveMatrix_general = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat_CalcReversePerspectiveMatrix);
            const math::Vec4 pos_proj_RevPersp = proj_mat_CalcReversePerspectiveMatrix * math::Vec4(0.0f, 0.0f, 500.0f, 1.0f);
            const math::Vec4 pos_ndc_RevPersp = pos_proj_RevPersp / pos_proj_RevPersp.w;
            const float view_z_RevPersp_1 = math::calc_view_z_from_ndc_z(pos_ndc_RevPersp.z, ndc_z_to_view_z_coef_CalcReversePerspectiveMatrix_general);

            const math::Mat44 proj_mat_CalcStandardPerspectiveMatrix = math::CalcStandardPerspectiveMatrix(view_info.camera_fov_y, view_info.aspect_ratio, view_info.near_z, view_info.far_z);
            const math::Vec4 ndc_z_to_view_z_coef_CalcStandardPerspectiveMatrix_general = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat_CalcStandardPerspectiveMatrix);
            const math::Vec4 pos_proj_StandardPersp = proj_mat_CalcStandardPerspectiveMatrix * math::Vec4(0.0f, 0.0f, 500.0f, 1.0f);
            const math::Vec4 pos_ndc_StandardPersp = pos_proj_StandardPersp / pos_proj_StandardPersp.w;
            const float view_z_StandardPersp_1 = math::calc_view_z_from_ndc_z(pos_ndc_StandardPersp.z, ndc_z_to_view_z_coef_CalcStandardPerspectiveMatrix_general);
            
            const math::Mat44 proj_mat_CalcStandardOrthographicMatrix = math::CalcStandardOrthographicSymmetricMatrix(100.0f, 100.0f, 0.1f, 1000.0f);
            const math::Vec4 ndc_z_to_view_z_coef_CalcStandardOrthographicMatrix = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat_CalcStandardOrthographicMatrix);
            const math::Vec4 pos_proj_StandardOrtho = proj_mat_CalcStandardOrthographicMatrix * math::Vec4(0.0f, 0.0f, 500.0f, 1.0f);
            const math::Vec4 pos_ndc_StandardOrtho = pos_proj_StandardOrtho / pos_proj_StandardOrtho.w;
            const float view_z_StandardOrtho_0 = math::calc_view_z_from_ndc_z(pos_ndc_StandardOrtho.z, ndc_z_to_view_z_coef_CalcStandardOrthographicMatrix);
            
            const math::Mat44 proj_mat_CalcReverseOrthographicMatrix = math::CalcReverseOrthographicSymmetricMatrix(100.0f, 100.0f, 0.1f, 1000.0f);
            const math::Vec4 ndc_z_to_view_z_coef_CalcReverseOrthographicMatrix = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat_CalcReverseOrthographicMatrix);
            const math::Vec4 pos_proj_ReverseOrtho = proj_mat_CalcReverseOrthographicMatrix * math::Vec4(0.0f, 0.0f, 500.0f, 1.0f);
            const math::Vec4 pos_ndc_ReverseOrtho = pos_proj_ReverseOrtho / pos_proj_ReverseOrtho.w;
            const float view_z_ReverseOrtho_0 = math::calc_view_z_from_ndc_z(pos_ndc_ReverseOrtho.z, ndc_z_to_view_z_coef_CalcReverseOrthographicMatrix);

		}

	}
}
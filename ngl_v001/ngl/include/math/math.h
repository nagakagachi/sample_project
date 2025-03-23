#pragma once

#include <cmath>
#include <memory>

#include <assert.h>

#include "detail/math_vector.h"
#include "detail/math_matrix.h"
#include "detail/math_util.h"

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
		
		// Standard Perspective Projection Matrix (default:LeftHand).
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcStandardPerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			float far_z,
			bool is_right_hand = k_defalut_right_hand_mode
		);
		
		// 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化.
		//	view_z = cb_ndc_z_to_view_z_coef.x / ( ndc_z * cb_ndc_z_to_view_z_coef.y + cb_ndc_z_to_view_z_coef.z )
		//		cb_ndc_z_to_view_z_coef = 
		//			Standard LH: ( far_z * near_z, near_z - far_z, far_z, 0.0)
		//			Standard RH: (-far_z * near_z, near_z - far_z, far_z, 0.0)
		inline constexpr Vec4 CalcViewDepthReconstructCoefForStandardPerspective(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(sign * far_z * near_z, near_z - far_z, far_z, 0.0);
			return coef;
		}

		// Reverse Perspective Projection Matrix.
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcReversePerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			float far_z,
			bool is_right_hand = k_defalut_right_hand_mode
		);
		
		// 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化.
		//	view_z = cb_ndc_z_to_view_z_coef.x / ( ndc_z * cb_ndc_z_to_view_z_coef.y + cb_ndc_z_to_view_z_coef.z )
		//		cb_ndc_z_to_view_z_coef = 
		//			Reverse LH: ( far_z * near_z, far_z - near_z, near_z, 0.0)
		//			Reverse RH: (-far_z * near_z, far_z - near_z, near_z, 0.0)
		inline constexpr Vec4 CalcViewDepthReconstructCoefForReversePerspective(float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(sign * far_z * near_z, far_z - near_z, near_z, 0.0);
			return coef;
		}

		// InfiniteFar and Reverse Z Perspective Projection Matrix.
		//	fov_y_radian : full angle of Vertical FOV.
		//	Z-> near:1, far:0
		//	https://thxforthefish.com/posts/reverse_z/
		Mat44 CalcReverseInfiniteFarPerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			bool is_right_hand = k_defalut_right_hand_mode
		);
		
		// 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化.
		//	view_z = cb_ndc_z_to_view_z_coef.x / ( ndc_z * cb_ndc_z_to_view_z_coef.y + cb_ndc_z_to_view_z_coef.z )
		//		cb_ndc_z_to_view_z_coef = 
		//			Infinite Far Reverse LH: ( near_z, 1.0, 0.0, 0.0)
		//			Infinite Far Reverse RH: (-near_z, 1.0, 0.0, 0.0)
		inline constexpr Vec4 CalcViewDepthReconstructCoefForInfiniteFarReversePerspective(float near_z, bool is_right_hand = k_defalut_right_hand_mode)
		{
			const float sign = (!is_right_hand) ? 1.0f : -1.0f;
			Vec4 coef(sign * near_z, 1.0f, 0.0f, 0.0);
			return coef;
		}


		// 標準平行投影.
		Mat44 CalcStandardOrthographicMatrix(float width, float height, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicMatrix(float left, float right, float bottom, float top, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);
		
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicMatrixSymmetric(float width, float height, float near_z, float far_z, bool is_right_hand = k_defalut_right_hand_mode);


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
		}

	}
}
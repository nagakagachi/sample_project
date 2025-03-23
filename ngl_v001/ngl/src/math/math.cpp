
#include "math/math.h"

namespace ngl
{
	namespace math
	{
		
		// View Matrix (LeftHand).
		Mat34 CalcViewMatrix(const Vec3& camera_location, const Vec3& forward, const Vec3& up, bool is_right_hand)
		{
			assert(!(forward == Vec3::Zero()));
			assert(!(up == Vec3::Zero()));

			Vec3 r2 = Vec3::Normalize(forward);
			if (is_right_hand)
				r2 = -r2;

			Vec3 r0 = Vec3::Cross(up, r2);
			r0 = Vec3::Normalize(r0);

			Vec3 r1 = Vec3::Cross(r2, r0);

			Vec3 neg_camera_location = -camera_location;
			float d0 = Vec3::Dot(r0, neg_camera_location);
			float d1 = Vec3::Dot(r1, neg_camera_location);
			float d2 = Vec3::Dot(r2, neg_camera_location);

			Mat34 M(
				Vec4(r0, d0),
				Vec4(r1, d1),
				Vec4(r2, d2)
			);

			return M;
		}
		
		// Standard Perspective Projection Matrix (default:LeftHand).
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcStandardPerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			float far_z,
			bool is_right_hand
		)
		{
			const float fov_y_half = fov_y_radian * 0.5f;
			const float fov_tan = std::sinf(fov_y_half) / std::cosf(fov_y_half); // std::tanf(fov_y_half);
			const float h = 1.0f / fov_tan;
			const float w = h / aspect_ratio;
			const float range_term = far_z / (far_z - near_z);
			const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;

			return Mat44(
				w,	0,	0,	0,
				0,	h,	0,	0,
				0,	0, z_sign * range_term, -near_z * range_term,
				0,	0, z_sign,	0
			);
		}

		// Reverse Perspective Projection Matrix (default:LeftHand).
		//	fov_y_radian : full angle of Vertical FOV.
		Mat44 CalcReversePerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			float far_z,
			bool is_right_hand
		)
		{
			const float fov_y_half = fov_y_radian * 0.5f;
			const float fov_tan = std::sinf(fov_y_half) / std::cosf(fov_y_half); // std::tanf(fov_y_half);
			const float h = 1.0f / fov_tan;
			const float w = h / aspect_ratio;
			const float range_term = far_z / (near_z - far_z);
			const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;

			return Mat44(
				w, 0, 0, 0,
				0, h, 0, 0,
				0, 0, z_sign * range_term, -near_z * range_term,
				0, 0, z_sign, 0
			);
		}

		// InfiniteFar and Reverse Z Perspective Projection Matrix (default:LeftHand).
		//	fov_y_radian : full angle of Vertical FOV.
		//	Z-> near:1, far:0
		//	https://thxforthefish.com/posts/reverse_z/
		Mat44 CalcReverseInfiniteFarPerspectiveMatrix
		(
			float fov_y_radian,
			float aspect_ratio,
			float near_z,
			bool is_right_hand
		)
		{
			const float fov_y_half = fov_y_radian * 0.5f;
			const float fov_tan = std::sinf(fov_y_half) / std::cosf(fov_y_half); // std::tanf(fov_y_half);
			const float h = 1.0f / fov_tan;
			const float w = h / aspect_ratio;
			const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;
			
			return Mat44(
				w, 0, 0, 0,
				0, h, 0, 0,
				0, 0, 0, near_z,
				0, 0, z_sign, 0
			);
		}
		
		// 標準平行投影.
		Mat44 CalcStandardOrthographicMatrix(float width, float height, float near_z, float far_z, bool is_right_hand)
		{
			const float w = 2.0f / width;
			const float h = 2.0f / height;
			const float range_term = 1.0f / (far_z - near_z);
			const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;

			return Mat44(
				w, 0, 0, 0,
				0, h, 0, 0,
				0, 0, z_sign * range_term, -near_z * range_term,
				0, 0, 0, 1
			);
		}
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicMatrix(float left, float right, float bottom, float top, float near_z, float far_z, bool is_right_hand)
		{
			const float r_w = 1.0f / (right - left);
			const float r_h = 1.0f / (top - bottom);
			const float range_term = 1.0f / (near_z - far_z);// Reverse.
			const float z_sign = (!is_right_hand) ? 1.0f : -1.0f;

			const float m_00 = (2.0f * r_w);
			const float m_11 = (2.0f * r_h);
			const float m_22 = z_sign * range_term;
			
			const float m_03 = -(left + right) * r_w;
			const float m_13 = -(bottom + top) * r_h;
			const float m_23 = -far_z * range_term;
			
			return Mat44(
				m_00, 0, 0, m_03,
				0, m_11, 0, m_13,
				0, 0, m_22, m_23,
				0, 0, 0, 1
			);
		}
		// Reverse平行投影.
		Mat44 CalcReverseOrthographicMatrixSymmetric(float width, float height, float near_z, float far_z, bool is_right_hand)
		{
			const float half_w = 0.5f*width;
			const float half_h = 0.5f*height;
			const auto m = CalcReverseOrthographicMatrix(-half_w, half_w, -half_h, half_h, near_z, far_z, is_right_hand);
			return m;
		}

	}
}
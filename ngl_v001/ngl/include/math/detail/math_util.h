#pragma once

#include <cmath>
#include <memory>

#include "math_vector.h"

namespace ngl
{
	namespace math
	{

		struct Plane
		{
			math::Vec3 normal_ = math::Vec3::UnitZ();
			float distance_ = 0.0f;

			Plane() = default;
			Plane(const math::Vec3& plane_pos/*平面の通る点*/, const math::Vec3& normal/*平面の法線*/)
			{
				normal_ = math::Vec3::Normalize(normal);
				distance_ = math::Vec3::Dot(normal_, plane_pos);
			}
		};
		struct Frustum
		{
			struct EPlaneIndex
			{
				enum : int
				{
					TOP_PLANE,
					BOTTOM_PLANE,
					LEFT_PLANE,
					RIGHT_PLANE,
					NEAR_PLANE,
					FAR_PLANE,
						
					_MAX
				};
			};
			Plane planes_[EPlaneIndex::_MAX];
		};
				
		// --
		inline void CreateFrustum(Frustum& out_frustum,
			math::Vec3 view_pos, math::Vec3 view_dir, math::Vec3 view_up, math::Vec3 view_right,
			float near_z, float far_z, float fov_y, float aspect_ratio)
		{
			Frustum     frustum;
			const float half_v = far_z * tanf(fov_y * 0.5f);
			const float half_h = half_v * aspect_ratio;
			const math::Vec3 front_mult_far = far_z * view_dir;

			frustum.planes_[Frustum::EPlaneIndex::NEAR_PLANE] = { view_pos + near_z * view_dir, view_dir };
			frustum.planes_[Frustum::EPlaneIndex::FAR_PLANE] = { view_pos + front_mult_far, -view_dir };
			frustum.planes_[Frustum::EPlaneIndex::RIGHT_PLANE] = { view_pos, math::Vec3::Cross(front_mult_far - view_right * half_h, view_up) };
			frustum.planes_[Frustum::EPlaneIndex::LEFT_PLANE] = { view_pos, math::Vec3::Cross(view_up,front_mult_far + view_right * half_h) };
			frustum.planes_[Frustum::EPlaneIndex::TOP_PLANE] = { view_pos, math::Vec3::Cross(view_right, front_mult_far - view_up * half_v) };
			frustum.planes_[Frustum::EPlaneIndex::BOTTOM_PLANE] = { view_pos, math::Vec3::Cross(front_mult_far + view_up * half_v, view_right) };

			out_frustum = frustum;
		}
		
		// 視点位置基準での奥行き方向1.0のFrustum頂点へのベクトル. 正規化されたベクトルではないことに注意(viwe_dir成分が1.0).
		// ex) view_dir + view_up*fov_term + view_right*fov_term*aspect
		struct ViewPositionRelativeFrustumCorners
		{
			struct ECornerIndex
			{
				enum : int
				{
					LEFT_TOP,
					RIGHT_TOP,
					RIGHT_BOTTOM,
					LEFT_BOTTOM,
						
					_MAX
				};
			};
			math::Vec3 corner_vec[4];
			math::Vec3 view_pos;
		};
		inline void CreateFrustumCorners(ViewPositionRelativeFrustumCorners& out_frustum_corners,
			math::Vec3 view_pos, math::Vec3 view_dir, math::Vec3 view_up, math::Vec3 view_right,
			float fov_y, float aspect_ratio)
		{
			ViewPositionRelativeFrustumCorners     corners;
			const float half_v = tanf(fov_y * 0.5f);
			const float half_h = half_v * aspect_ratio;
			const math::Vec3 front_mult_top = half_v * view_up;
			const math::Vec3 front_mult_right = half_h * view_right;

			corners.view_pos = view_pos;
			corners.corner_vec[ViewPositionRelativeFrustumCorners::ECornerIndex::LEFT_TOP] = (view_dir + front_mult_top - front_mult_right);
			corners.corner_vec[ViewPositionRelativeFrustumCorners::ECornerIndex::RIGHT_TOP] = (view_dir + front_mult_top + front_mult_right);
			corners.corner_vec[ViewPositionRelativeFrustumCorners::ECornerIndex::RIGHT_BOTTOM] = (view_dir - front_mult_top + front_mult_right);
			corners.corner_vec[ViewPositionRelativeFrustumCorners::ECornerIndex::LEFT_BOTTOM] = (view_dir - front_mult_top - front_mult_right);
			
			out_frustum_corners = corners;
		}

	}
}

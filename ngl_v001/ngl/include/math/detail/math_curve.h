#pragma once

#include <cmath>
#include <memory>

namespace ngl
{
	namespace math
	{
		// Cubic Hermite Spline Curve.
		//	t==0 で 値y0, 勾配d0
		//	t==1 で 値y1, 勾配d1
		//	となるような3次曲線.
		//	定義域を正規化して入力する場合は正規化係数の逆数を勾配に乗ずる必要がある点に注意. https://nagakagachi.hatenablog.com/
		constexpr float CubicHermite(float t, const float y0, const float y1, const float d0, const float d1)
		{
			auto k_a = 2.0f * y0 - 2.0f * y1 + d0 + d1;
			auto k_b = -3.0f * y0 + 3.0f * y1 - 2.0f * d0 - d1;
			auto k_c = d0;
			auto k_d = y0;
			return k_a * t*t*t + k_b * t*t + k_c * t + k_d;
		}

		static void CubicHermite_Test()
		{
			constexpr auto y0 = 0.0f;
			constexpr auto y1 = 1.0f;
			constexpr auto d0 = 1.0f;
			constexpr auto d1 = 1.0f;
	
			constexpr auto hv0 = CubicHermite(0.0f, y0, y1, d0 ,d1);
			static_assert(0.0f == hv0);
			constexpr auto hv1 = CubicHermite(0.5f, y0, y1, d0 ,d1);
			static_assert(0.5f == hv1);
			constexpr auto hv2 = CubicHermite(1.0f, y0, y1, d0 ,d1);
			static_assert(1.0f == hv2);
		}

	}
}

#pragma once

#include <cmath>
#include <memory>

#include "math_vector.h"

namespace ngl
{
	namespace math
	{

		// -----------------------------------------------------------------------------------------
		struct Mat22;
		inline constexpr Mat22 operator+(const Mat22& m0, const Mat22& m1);
		inline constexpr Mat22 operator-(const Mat22& m0, const Mat22& m1);
		inline constexpr Mat22 operator*(const Mat22& m0, const Mat22& m1);
		inline constexpr Mat22 operator*(const Mat22& m, float s);
		inline constexpr Mat22 operator*(float s, const Mat22& m);
		inline constexpr Mat22 operator/(const Mat22& m, float s);
		
		struct Mat22
		{
			union
			{
				float	m[2][2];
				struct
				{
					Vec2	r0, r1;
				};
			};

			Mat22() = default;
			constexpr Mat22(const Mat22& v) = default;
			explicit constexpr Mat22(float v)
				: r0(v), r1(v)
			{
			}
			constexpr Mat22(
				float _m00, float _m01,
				float _m10, float _m11)
				: r0(_m00, _m01)
				, r1(_m10, _m11)
			{
			}
			constexpr Mat22(const Vec2& row0, const Vec2& row1)
				: r0(row0)
				, r1(row1)
			{
			}
			Vec2 GetColumn0() const
			{
				return Vec2(r0.x, r1.x);
			}
			Vec2 GetColumn1() const
			{
				return Vec2(r0.y, r1.y);
			}

			Mat22& SetColumn0(const Vec2& v)
			{
				r0.x = v.x;
				r1.x = v.y;
				return *this;
			}
			Mat22& SetColumn1(const Vec2& v)
			{
				r0.y = v.x;
				r1.y = v.y;
				return *this;
			}
			// 対角要素を設定. 3x4行列の場合は左上3x3を対象.
			Mat22& SetDiagonal(const Vec2& v)
			{
				r0.x = v.x;
				r1.y = v.y;
				return *this;
			}

			static constexpr Mat22 Zero()
			{
				return Mat22(0.0f);
			}
			static constexpr Mat22 Identity()
			{
				return {
					Vec2::UnitX(),
					Vec2::UnitY(),
				};
			}

			// 転置
			static constexpr Mat22 Transpose(const Mat22& m)
			{
				return Mat22(
					m.r0.x, m.r1.x,
					m.r0.y, m.r1.y
				);
			}
			// 行列式.
			static constexpr float Determinant(const Mat22& m)
			{
				return m.r0.x * m.r1.y - m.r0.y * m.r1.x;
			}
			// 逆行列 または 行列式で割らずに余因子行列を返す.
			template<bool IS_COFACTOR>
			static constexpr Mat22 InverseOrCofactor(const Mat22& m)
			{
				const float c00 = m.r1.y;
				const float c01 = -m.r1.x;

				const float c10 = -m.r0.y;
				const float c11 = m.r0.x;


				if constexpr (IS_COFACTOR)
				{
					const auto cofactor = Mat22(
						c00, c01,
						c10, c11
					);
					return cofactor;// 余因子行列のまま返す.
				}
				else
				{
					// cofactorの転置.
					const auto adj = Mat22(
						c00, c10,
						c01, c11
					);
					const float det = m.r0.x * c00 + m.r0.y * c01;
					const float inv_det = 1.0f / det;
					return adj * inv_det;// 行列式で割って逆行列として返す.
				}
			}
			// 逆行列.
			static constexpr Mat22 Inverse(const Mat22& m)
			{
				return InverseOrCofactor<false>(m);
			}
			// 余因子行列. 逆行列計算時に行列式で割らない値を返す.
			//	メッシュの法線トランスフォーム時に不均一スケールで正しい変換をする際に逆転置行列ではなく余因子行列を利用する場合など.
			static constexpr Mat22 Cofactor(const Mat22& m)
			{
				return InverseOrCofactor<true>(m);
			}
			
			// 回転行列.
			static Mat22 Rot(float radian)
			{
				return Mat22(
					std::cosf(radian), -std::sinf(radian),
					std::sinf(radian), std::cosf(radian)
				);
			}
		};
		// Mat + Mat
		inline constexpr Mat22 operator+(const Mat22& m0, const Mat22& m1)
		{
			return Mat22(
				m0.r0 + m1.r0,
				m0.r1 + m1.r1
			);
		}
		// Mat - Mat
		inline constexpr Mat22 operator-(const Mat22& m0, const Mat22& m1)
		{
			return Mat22(
				m0.r0 - m1.r0,
				m0.r1 - m1.r1
			);
		}
		// Mat * Mat
		inline constexpr Mat22 operator*(const Mat22& m0, const Mat22& m1)
		{
			return Mat22(
				m0.r0.x * m1.r0 + m0.r0.y * m1.r1,
				m0.r1.x * m1.r0 + m0.r1.y * m1.r1
			);
		}
		// Mat * scalar
		inline constexpr Mat22 operator*(const Mat22& m, float s)
		{
			return Mat22(
				m.r0 * s,
				m.r1 * s
			);
		}
		// scalar * Mat
		inline constexpr Mat22 operator*(float s, const Mat22& m)
		{
			return m * s;
		}
		// Mat / scalar
		inline constexpr Mat22 operator/(const Mat22& m, float s)
		{
			const auto s_inv = 1.0f / s;
			return m * s_inv;
		}
		// -----------------------------------------------------------------------------------------

		
		// -----------------------------------------------------------------------------------------
		struct Mat33;
		inline constexpr Mat33 operator+(const Mat33& m0, const Mat33& m1);
		inline constexpr Mat33 operator-(const Mat33& m0, const Mat33& m1);
		inline constexpr Mat33 operator*(const Mat33& m0, const Mat33& m1);
		inline constexpr Mat33 operator*(const Mat33& m, float s);
		inline constexpr Mat33 operator*(float s, const Mat33& m);
		inline constexpr Mat33 operator/(const Mat33& m, float s);
		
		struct Mat33
		{
			union
			{
				float	m[3][3];
				struct
				{
					Vec3	r0, r1, r2;
				};
			};

			Mat33() = default;
			constexpr Mat33(const Mat33& v) = default;
			explicit constexpr Mat33(float v)
				: r0(v), r1(v), r2(v)
			{
			}
			constexpr Mat33(
				float _m00, float _m01, float _m02,
				float _m10, float _m11, float _m12,
				float _m20, float _m21, float _m22)
				: r0(_m00, _m01, _m02)
				, r1(_m10, _m11, _m12)
				, r2(_m20, _m21, _m22)
			{
			}
			constexpr Mat33(const Vec3& row0, const Vec3& row1, const Vec3& row2)
				: r0(row0)
				, r1(row1)
				, r2(row2)
			{
			}
			Vec3 GetColumn0() const
			{
				return Vec3(r0.x, r1.x, r2.x);
			}
			Vec3 GetColumn1() const
			{
				return Vec3(r0.y, r1.y, r2.y);
			}
			Vec3 GetColumn2() const
			{
				return Vec3(r0.z, r1.z, r2.z);
			}
			Mat33& SetColumn0(const Vec3& v)
			{
				r0.x = v.x;
				r1.x = v.y;
				r2.x = v.z;
				return *this;
			}
			Mat33& SetColumn1(const Vec3& v)
			{
				r0.y = v.x;
				r1.y = v.y;
				r2.y = v.z;
				return *this;
			}
			Mat33& SetColumn2(const Vec3& v)
			{
				r0.z = v.x;
				r1.z = v.y;
				r2.z = v.z;
				return *this;
			}
			// 対角要素を設定. 3x4行列の場合は左上3x3を対象.
			Mat33& SetDiagonal(const Vec3& v)
			{
				r0.x = v.x;
				r1.y = v.y;
				r2.z = v.z;
				return *this;
			}

			static constexpr Mat33 Zero()
			{
				return Mat33(0.0f);
			}
			static constexpr Mat33 Identity()
			{
				return {
					Vec3::UnitX(),
					Vec3::UnitY(),
					Vec3::UnitZ()
				};
			}

			// 転置
			static constexpr Mat33 Transpose(const Mat33& m)
			{
				return Mat33(
					m.r0.x, m.r1.x, m.r2.x,
					m.r0.y, m.r1.y, m.r2.y,
					m.r0.z, m.r1.z, m.r2.z
				);
			}
			// 行列式.
			static constexpr float Determinant(const Mat33& m)
			{
				const float c00 = Mat22::Determinant(Mat22(m.r1.y, m.r1.z, m.r2.y, m.r2.z));
				const float c01 = -Mat22::Determinant(Mat22(m.r1.x, m.r1.z, m.r2.x, m.r2.z));
				const float c02 = Mat22::Determinant(Mat22(m.r1.x, m.r1.y, m.r2.x, m.r2.y));
				return m.r0.x * c00 + m.r0.y * c01 + m.r0.z * c02;
			}
			// 逆行列 または 行列式で割らずに余因子行列を返す.
			template<bool IS_COFACTOR>
			static constexpr Mat33 InverseOrCofactor(const Mat33& m)
			{
				const float c00 = Mat22::Determinant(Mat22(m.r1.y, m.r1.z, m.r2.y, m.r2.z));
				const float c01 = -Mat22::Determinant(Mat22(m.r1.x, m.r1.z, m.r2.x, m.r2.z));
				const float c02 = Mat22::Determinant(Mat22(m.r1.x, m.r1.y, m.r2.x, m.r2.y));

				const float c10 = -Mat22::Determinant(Mat22(m.r0.y, m.r0.z, m.r2.y, m.r2.z));
				const float c11 = Mat22::Determinant(Mat22(m.r0.x, m.r0.z, m.r2.x, m.r2.z));
				const float c12 = -Mat22::Determinant(Mat22(m.r0.x, m.r0.y, m.r2.x, m.r2.y));

				const float c20 = Mat22::Determinant(Mat22(m.r0.y, m.r0.z, m.r1.y, m.r1.z));
				const float c21 = -Mat22::Determinant(Mat22(m.r0.x, m.r0.z, m.r1.x, m.r1.z));
				const float c22 = Mat22::Determinant(Mat22(m.r0.x, m.r0.y, m.r1.x, m.r1.y));


				if constexpr (IS_COFACTOR)
				{
					const auto cofactor = Mat33(
						c00, c01, c02,
						c10, c11, c12,
						c20, c21, c22
					);
					return cofactor;// 余因子行列のまま返す.
				}
				else
				{
					// cofactorの転置.
					const auto adj = Mat33(
						c00, c10, c20,
						c01, c11, c21,
						c02, c12, c22
					);
					const float det = m.r0.x * c00 + m.r0.y * c01 + m.r0.z * c02;
					const float inv_det = 1.0f / det;
					return adj * inv_det;// 行列式で割って逆行列として返す.
				}
			}
			// 逆行列.
			static constexpr Mat33 Inverse(const Mat33& m)
			{
				return InverseOrCofactor<false>(m);
			}
			// 余因子行列. 逆行列計算時に行列式で割らない値を返す.
			//	メッシュの法線トランスフォーム時に不均一スケールで正しい変換をする際に逆転置行列ではなく余因子行列を利用する場合など.
			static constexpr Mat33 Cofactor(const Mat33& m)
			{
				return InverseOrCofactor<true>(m);
			}

			// X軸回転行列.
			static Mat33 RotAxisX(float radian)
			{
				return Mat33(
					1.0f, 0.0f, 0.0f,
					0.0f, std::cosf(radian), -std::sinf(radian),
					0.0f, std::sinf(radian), std::cosf(radian)
				);
			}
			// Y軸回転行列.
			static Mat33 RotAxisY(float radian)
			{
				return Mat33(
					std::cosf(radian), 0.0f, std::sinf(radian),
					0.0f, 1.0f, 0.0f,
					-std::sinf(radian), 0.0f, std::cosf(radian)
				);
			}
			// Z軸回転行列.
			static Mat33 RotAxisZ(float radian)
			{
				return Mat33(
					std::cosf(radian), -std::sinf(radian), 0.0f,
					std::sinf(radian), std::cosf(radian), 0.0f,
					0.0f, 0.0f, 1.0f
				);
			}
		};
		// Mat + Mat
		inline constexpr Mat33 operator+(const Mat33& m0, const Mat33& m1)
		{
			return Mat33(
				m0.r0 + m1.r0,
				m0.r1 + m1.r1,
				m0.r2 + m1.r2
			);
		}
		// Mat - Mat
		inline constexpr Mat33 operator-(const Mat33& m0, const Mat33& m1)
		{
			return Mat33(
				m0.r0 - m1.r0,
				m0.r1 - m1.r1,
				m0.r2 - m1.r2
			);
		}
		// Mat * Mat
		inline constexpr Mat33 operator*(const Mat33& m0, const Mat33& m1)
		{
			return Mat33(
				m0.r0.x * m1.r0 + m0.r0.y * m1.r1 + m0.r0.z * m1.r2,
				m0.r1.x * m1.r0 + m0.r1.y * m1.r1 + m0.r1.z * m1.r2,
				m0.r2.x * m1.r0 + m0.r2.y * m1.r1 + m0.r2.z * m1.r2
			);
		}
		// Mat * scalar
		inline constexpr Mat33 operator*(const Mat33& m, float s)
		{
			return Mat33(
				m.r0 * s,
				m.r1 * s,
				m.r2 * s
			);
		}
		// scalar * Mat
		inline constexpr Mat33 operator*(float s, const Mat33& m)
		{
			return m * s;
		}
		// Mat / scalar
		inline constexpr Mat33 operator/(const Mat33& m, float s)
		{
			const auto s_inv = 1.0f / s;
			return m * s_inv;
		}
		// -----------------------------------------------------------------------------------------


		// -----------------------------------------------------------------------------------------
		struct Mat44;
		inline constexpr Mat44 operator+(const Mat44& m0, const Mat44& m1);
		inline constexpr Mat44 operator-(const Mat44& m0, const Mat44& m1);
		inline constexpr Mat44 operator*(const Mat44& m0, const Mat44& m1);
		inline constexpr Mat44 operator*(const Mat44& m, float s);
		inline constexpr Mat44 operator*(float s, const Mat44& m);
		inline constexpr Mat44 operator/(const Mat44& m, float s);
		
		struct Mat44
		{
			union
			{
				float	m[4][4];
				struct
				{
					Vec4	r0, r1, r2, r3;
				};
			};

			Mat44() = default;
			constexpr Mat44(const Mat44& v) = default;
			explicit constexpr Mat44(float v)
				: r0(v), r1(v), r2(v), r3(v)
			{
			}
			constexpr Mat44(float _x, float _y, float _z, float _w)
				: r0(_x, _y, _z, _w)
				, r1(_x, _y, _z, _w)
				, r2(_x, _y, _z, _w)
				, r3(_x, _y, _z, _w)
			{
			}
			constexpr Mat44(
				float _m00, float _m01, float _m02, float _m03,
				float _m10, float _m11, float _m12, float _m13,
				float _m20, float _m21, float _m22, float _m23,
				float _m30, float _m31, float _m32, float _m33)
				: r0(_m00, _m01, _m02, _m03)
				, r1(_m10, _m11, _m12, _m13)
				, r2(_m20, _m21, _m22, _m23)
				, r3(_m30, _m31, _m32, _m33)
			{
			}
			constexpr Mat44(const Vec4& row0, const Vec4& row1, const Vec4& row2, const Vec4& row3)
				: r0(row0)
				, r1(row1)
				, r2(row2)
				, r3(row3)
			{
			}
			// from Mat33.
			explicit constexpr Mat44(const Mat33& m)
				: r0(m.r0, 0.0f), r1(m.r1, 0.0f), r2(m.r2, 0.0f), r3(0.0f)
			{
			}
			Vec4 GetColumn0() const
			{
				return Vec4(r0.x, r1.x, r2.x, r3.x);
			}
			Vec4 GetColumn1() const
			{
				return Vec4(r0.y, r1.y, r2.y, r3.y);
			}
			Vec4 GetColumn2() const
			{
				return Vec4(r0.z, r1.z, r2.z, r3.z);
			}
			Vec4 GetColumn3() const
			{
				return Vec4(r0.w, r1.w, r2.w, r3.w);
			}
			Mat44& SetColumn0(const Vec4& v)
			{
				r0.x = v.x;
				r1.x = v.y;
				r2.x = v.z;
				r3.x = v.w;
				return *this;
			}
			Mat44& SetColumn1(const Vec4& v)
			{
				r0.y = v.x;
				r1.y = v.y;
				r2.y = v.z;
				r3.y = v.w;
				return *this;
			}
			Mat44& SetColumn2(const Vec4& v)
			{
				r0.z = v.x;
				r1.z = v.y;
				r2.z = v.z;
				r3.z = v.w;
				return *this;
			}
			Mat44& SetColumn3(const Vec4& v)
			{
				r0.w = v.x;
				r1.w = v.y;
				r2.w = v.z;
				r3.w = v.w;
				return *this;
			}
			// 対角要素を設定. 3x4行列の場合は左上3x3を対象.
			Mat44& SetDiagonal(const Vec4& v)
			{
				r0.x = v.x;
				r1.y = v.y;
				r2.z = v.z;
				r2.w = v.w;
				return *this;
			}


			static constexpr Mat44 Zero()
			{
				return Mat44(0.0f);
			}
			static constexpr Mat44 Identity()
			{
				return {
					Vec4::UnitX(),
					Vec4::UnitY(),
					Vec4::UnitZ(),
					Vec4::UnitW(),
				};
			}

			// 転置
			static constexpr Mat44 Transpose(const Mat44& m)
			{
				return Mat44(
					m.r0.x, m.r1.x, m.r2.x, m.r3.x,
					m.r0.y, m.r1.y, m.r2.y, m.r3.y,
					m.r0.z, m.r1.z, m.r2.z, m.r3.z,
					m.r0.w, m.r1.w, m.r2.w, m.r3.w
				);
			}

			// 行列式.
			static constexpr float Determinant(const Mat44& m)
			{
				const float c00 = Mat33::Determinant(Mat33(m.r1.y, m.r1.z, m.r1.w, m.r2.y, m.r2.z, m.r2.w, m.r3.y, m.r3.z, m.r3.w));
				const float c01 = -Mat33::Determinant(Mat33(m.r1.x, m.r1.z, m.r1.w, m.r2.x, m.r2.z, m.r2.w, m.r3.x, m.r3.z, m.r3.w));
				const float c02 = Mat33::Determinant(Mat33(m.r1.x, m.r1.y, m.r1.w, m.r2.x, m.r2.y, m.r2.w, m.r3.x, m.r3.y, m.r3.w));
				const float c03 = -Mat33::Determinant(Mat33(m.r1.x, m.r1.y, m.r1.z, m.r2.x, m.r2.y, m.r2.z, m.r3.x, m.r3.y, m.r3.z));
				const float det = m.r0.x * c00 + m.r0.y * c01 + m.r0.z * c02 + m.r0.w * c03;
				return det;
			}
			// 逆行列 または 行列式で割らずに余因子行列を返す.
			template<bool IS_COFACTOR>
			static constexpr Mat44 InverseOrCofactor(const Mat44& m)
			{
				const float c00 = Mat33::Determinant(Mat33(m.r1.y, m.r1.z, m.r1.w, m.r2.y, m.r2.z, m.r2.w, m.r3.y, m.r3.z, m.r3.w));
				const float c01 = -Mat33::Determinant(Mat33(m.r1.x, m.r1.z, m.r1.w, m.r2.x, m.r2.z, m.r2.w, m.r3.x, m.r3.z, m.r3.w));
				const float c02 = Mat33::Determinant(Mat33(m.r1.x, m.r1.y, m.r1.w, m.r2.x, m.r2.y, m.r2.w, m.r3.x, m.r3.y, m.r3.w));
				const float c03 = -Mat33::Determinant(Mat33(m.r1.x, m.r1.y, m.r1.z, m.r2.x, m.r2.y, m.r2.z, m.r3.x, m.r3.y, m.r3.z));

				const float c10 = -Mat33::Determinant(Mat33(m.r0.y, m.r0.z, m.r0.w, m.r2.y, m.r2.z, m.r2.w, m.r3.y, m.r3.z, m.r3.w));
				const float c11 = Mat33::Determinant(Mat33(m.r0.x, m.r0.z, m.r0.w, m.r2.x, m.r2.z, m.r2.w, m.r3.x, m.r3.z, m.r3.w));
				const float c12 = -Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.w, m.r2.x, m.r2.y, m.r2.w, m.r3.x, m.r3.y, m.r3.w));
				const float c13 = Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.z, m.r2.x, m.r2.y, m.r2.z, m.r3.x, m.r3.y, m.r3.z));

				const float c20 = Mat33::Determinant(Mat33(m.r0.y, m.r0.z, m.r0.w, m.r1.y, m.r1.z, m.r1.w, m.r3.y, m.r3.z, m.r3.w));
				const float c21 = -Mat33::Determinant(Mat33(m.r0.x, m.r0.z, m.r0.w, m.r1.x, m.r1.z, m.r1.w, m.r3.x, m.r3.z, m.r3.w));
				const float c22 = Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.w, m.r1.x, m.r1.y, m.r1.w, m.r3.x, m.r3.y, m.r3.w));
				const float c23 = -Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.z, m.r1.x, m.r1.y, m.r1.z, m.r3.x, m.r3.y, m.r3.z));

				const float c30 = -Mat33::Determinant(Mat33(m.r0.y, m.r0.z, m.r0.w, m.r1.y, m.r1.z, m.r1.w, m.r2.y, m.r2.z, m.r2.w));
				const float c31 = Mat33::Determinant(Mat33(m.r0.x, m.r0.z, m.r0.w, m.r1.x, m.r1.z, m.r1.w, m.r2.x, m.r2.z, m.r2.w));
				const float c32 = -Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.w, m.r1.x, m.r1.y, m.r1.w, m.r2.x, m.r2.y, m.r2.w));
				const float c33 = Mat33::Determinant(Mat33(m.r0.x, m.r0.y, m.r0.z, m.r1.x, m.r1.y, m.r1.z, m.r2.x, m.r2.y, m.r2.z));


				if constexpr (IS_COFACTOR)
				{
					const auto cofactor = Mat44(
						c00, c01, c02, c03,
						c10, c11, c12, c13,
						c20, c21, c22, c23,
						c30, c31, c32, c33
					);
					return cofactor;// 余因子行列のまま返す.
				}
				else
				{
					// cofactorの転置.
					const auto adj = Mat44(
						c00, c10, c20, c30,
						c01, c11, c21, c31,
						c02, c12, c22, c32,
						c03, c13, c23, c33
					);
					const float det = m.r0.x * c00 + m.r0.y * c01 + m.r0.z * c02 + m.r0.w * c03;
					const float inv_det = 1.0f / det;
					return adj * inv_det;// 行列式で割って逆行列として返す.
				}
			}
			// 逆行列.
			static constexpr Mat44 Inverse(const Mat44& m)
			{
				return InverseOrCofactor<false>(m);
			}
			// 余因子行列. 逆行列計算時に行列式で割らない値を返す.
			//	メッシュの法線トランスフォーム時に不均一スケールで正しい変換をする際に逆転置行列ではなく余因子行列を利用する場合など.
			//	https://github.com/graphitemaster/normals_revisited
			static constexpr Mat44 Cofactor(const Mat44& m)
			{
				return InverseOrCofactor<true>(m);
			}

			// X軸回転行列.
			static Mat44 RotAxisX(float radian)
			{
				return Mat44(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, std::cosf(radian), -std::sinf(radian), 0.0f,
					0.0f, std::sinf(radian), std::cosf(radian), 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				);
			}
			// Y軸回転行列.
			static Mat44 RotAxisY(float radian)
			{
				return Mat44(
					std::cosf(radian), 0.0f, std::sinf(radian), 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					-std::sinf(radian), 0.0f, std::cosf(radian), 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				);
			}
			// Z軸回転行列.
			static Mat44 RotAxisZ(float radian)
			{
				return Mat44(
					std::cosf(radian), -std::sinf(radian), 0.0f, 0.0f,
					std::sinf(radian), std::cosf(radian), 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				);
			}
		};

		// Mat + Mat
		inline constexpr Mat44 operator+(const Mat44& m0, const Mat44& m1)
		{
			return Mat44(
				m0.r0 + m1.r0,
				m0.r1 + m1.r1,
				m0.r2 + m1.r2,
				m0.r3 + m1.r3
			);
		}
		// Mat - Mat
		inline constexpr Mat44 operator-(const Mat44& m0, const Mat44& m1)
		{
			return Mat44(
				m0.r0 - m1.r0,
				m0.r1 - m1.r1,
				m0.r2 - m1.r2,
				m0.r3 - m1.r3
			);
		}
		// Mat * Mat
		inline constexpr Mat44 operator*(const Mat44& m0, const Mat44& m1)
		{
			return Mat44(
				m0.r0.x * m1.r0 + m0.r0.y * m1.r1 + m0.r0.z * m1.r2 + m0.r0.w * m1.r3,
				m0.r1.x * m1.r0 + m0.r1.y * m1.r1 + m0.r1.z * m1.r2 + m0.r1.w * m1.r3,
				m0.r2.x * m1.r0 + m0.r2.y * m1.r1 + m0.r2.z * m1.r2 + m0.r2.w * m1.r3,
				m0.r3.x * m1.r0 + m0.r3.y * m1.r1 + m0.r3.z * m1.r2 + m0.r3.w * m1.r3
			);
		}
		// Mat * scalar
		inline constexpr Mat44 operator*(const Mat44& m, float s)
		{
			return Mat44(
				m.r0 * s,
				m.r1 * s,
				m.r2 * s,
				m.r3 * s
			);
		}
		// scalar * Mat
		inline constexpr Mat44 operator*(float s, const Mat44& m)
		{
			return m * s;
		}
		// Mat / scalar
		inline constexpr Mat44 operator/(const Mat44& m, float s)
		{
			const auto s_inv = 1.0f / s;
			return m * s_inv;
		}
		// -----------------------------------------------------------------------------------------


		// -----------------------------------------------------------------------------------------
		// row-major Transform Matrix34 が必要な場合の Mat44->Mat34 コンバート用.
		struct Mat34
		{
			union
			{
				float	m[3][4];
				struct
				{
					Vec4	r0, r1, r2;
				};
			};

			Mat34() = default;
			constexpr Mat34(const Mat34& v) = default;
			explicit constexpr Mat34(float v)
				: r0(v), r1(v), r2(v)
			{
			}
			constexpr Mat34(float _x, float _y, float _z, float _w)
				: r0(_x, _y, _z, _w), r1(_x, _y, _z, _w), r2(_x, _y, _z, _w)
			{
			}
			constexpr Mat34(
				float _m00, float _m01, float _m02, float _m03,
				float _m10, float _m11, float _m12, float _m13,
				float _m20, float _m21, float _m22, float _m23)
				: r0(_m00, _m01, _m02, _m03), r1(_m10, _m11, _m12, _m13), r2(_m20, _m21, _m22, _m23)
			{
			}
			constexpr Mat34(const Vec4& row0, const Vec4& row1, const Vec4& row2)
				: r0(row0), r1(row1), r2(row2)
			{
			}
			// from Mat44.
			explicit constexpr Mat34(const Mat44& m)
				: r0(m.r0), r1(m.r1), r2(m.r2)
			{
			}
			// from Mat33.
			explicit constexpr Mat34(const Mat33& m)
				: r0(m.r0.x, m.r0.y, m.r0.z, 0.0f), r1(m.r1.x, m.r1.y, m.r1.z, 0.0f), r2(m.r2.x, m.r2.y, m.r2.z, 0.0f)
			{
			}
			// to Mat33.
			constexpr Mat33 GetMat33()
			{
				return Mat33(r0.XYZ(), r1.XYZ(), r2.XYZ());
			}
			Vec3 GetColumn0() const
			{
				return Vec3(r0.x, r1.x, r2.x);
			}
			Vec3 GetColumn1() const
			{
				return Vec3(r0.y, r1.y, r2.y);
			}
			Vec3 GetColumn2() const
			{
				return Vec3(r0.z, r1.z, r2.z);
			}
			Vec3 GetColumn3() const
			{
				return Vec3(r0.w, r1.w, r2.w);
			}

			Mat34& SetColumn0(const Vec3& v)
			{
				r0.x = v.x;
				r1.x = v.y;
				r2.x = v.z;
				return *this;
			}
			Mat34& SetColumn1(const Vec3& v)
			{
				r0.y = v.x;
				r1.y = v.y;
				r2.y = v.z;
				return *this;
			}
			Mat34& SetColumn2(const Vec3& v)
			{
				r0.z = v.x;
				r1.z = v.y;
				r2.z = v.z;
				return *this;
			}
			Mat34& SetColumn3(const Vec3& v)
			{
				r0.w = v.x;
				r1.w = v.y;
				r2.w = v.z;
				return *this;
			}
			// 対角要素を設定. 3x4行列の場合は左上3x3を対象.
			Mat34& SetDiagonal(const Vec3& v)
			{
				r0.x = v.x;
				r1.y = v.y;
				r2.z = v.z;
				return *this;
			}

			static constexpr Mat34 Zero()
			{
				return Mat34(0.0f);
			}
			static constexpr Mat34 Identity()
			{
				return {
					Vec4::UnitX(),
					Vec4::UnitY(),
					Vec4::UnitZ(),
				};
			}

			// 逆変換.
			//  Mat34同士の乗算は未サポートだが, 以下のように元のMat34で変換されたベクトルを戻すような逆変換を計算する.
			//  p = Inverse(Mat34_A) * (Mat34_A * p)
			static constexpr Mat34 Inverse(const Mat34& m)
			{
				const Mat33 inv33 = Mat33::Inverse(Mat33(m.r0.XYZ(), m.r1.XYZ(), m.r2.XYZ()));

				const Vec3 n_trans(-m.r0.w, -m.r1.w, -m.r2.w);

				const float nx = ngl::math::Vec3::Dot(inv33.r0, n_trans);
				const float ny = ngl::math::Vec3::Dot(inv33.r1, n_trans);
				const float nz = ngl::math::Vec3::Dot(inv33.r2, n_trans);

				return {
					Vec4(inv33.r0, nx),
					Vec4(inv33.r1, ny),
					Vec4(inv33.r2, nz),
				};
			}

			// X軸回転行列.
			static Mat34 RotAxisX(float radian)
			{
				return Mat34(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, std::cosf(radian), -std::sinf(radian), 0.0f,
					0.0f, std::sinf(radian), std::cosf(radian), 0.0f
				);
			}
			// Y軸回転行列.
			static Mat34 RotAxisY(float radian)
			{
				return Mat34(
					std::cosf(radian), 0.0f, std::sinf(radian), 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					-std::sinf(radian), 0.0f, std::cosf(radian), 0.0f
				);
			}
			// Z軸回転行列.
			static Mat34 RotAxisZ(float radian)
			{
				return Mat34(
					std::cosf(radian), -std::sinf(radian), 0.0f, 0.0f,
					std::sinf(radian), std::cosf(radian), 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f
				);
			}


			bool operator==(const Mat34& m)
			{
				return (r0 == m.r0) && (r1 == m.r1) && (r2 == m.r2);
			}
			bool operator!=(const Mat34& m)
			{
				return !(*this == m);
			}
		};
	}
}
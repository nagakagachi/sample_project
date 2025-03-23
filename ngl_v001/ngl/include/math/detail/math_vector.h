#pragma once

#include <cmath>
#include <memory>

namespace ngl
{
	namespace math
	{

		template<int N>
		struct VecComponentT
		{
		};

		// Vec2 Data.
		template<>
		struct VecComponentT<2>
		{
			// data.
			union
			{
				struct
				{
					float x, y;
				};
				float data[2];
			};

			VecComponentT() = default;
			constexpr VecComponentT(const VecComponentT& v) = default;
			explicit constexpr VecComponentT(float v)
				: x(v), y(v)
			{}
			constexpr VecComponentT(float _x, float _y, float _dummy0, float _dummy1)
				: x(_x), y(_y)
			{}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(float _x, float _y, float _dummy0, float _dummy1, ComponentModifyFunc func)
				: x(func(_x)), y(func(_y))
			{}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(const float* _data, ComponentModifyFunc func)
				: x(func(_data[0])), y(func(_data[1]))
			{}
		};

		// Vec3 Data.
		template<>
		struct VecComponentT<3>
		{
			// data.
			union
			{
				struct
				{
					float x, y, z;
				};
				float data[3];
			};

			VecComponentT() = default;
			constexpr VecComponentT(const VecComponentT& v) = default;
			explicit constexpr VecComponentT(float v)
				: x(v), y(v), z(v)
			{}
			constexpr VecComponentT(float _x, float _y, float _z, float _dummy0)
				: x(_x), y(_y), z(_z)
			{}
			// 一つ次元の少ないVectorで初期化.
			constexpr VecComponentT(const VecComponentT<2>&v, float _z)
				: x(v.x), y(v.y), z(_z)
			{}

			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(float _x, float _y, float _z, float _dummy0, ComponentModifyFunc func)
				: x(func(_x)), y(func(_y)), z(func(_z))
			{}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(const float* _data, ComponentModifyFunc func)
				: x(func(_data[0])), y(func(_data[1])), z(func(_data[2]))
			{}
		};

		// Vec4 Data.
		template<>
		struct VecComponentT<4>
		{
			// data.
			union
			{
				struct
				{
					float x, y, z, w;
				};
				float data[4];
			};

			VecComponentT() = default;
			constexpr VecComponentT(const VecComponentT& v) = default;
			explicit constexpr VecComponentT(float v)
				: x(v), y(v), z(v), w(v)
			{}
			constexpr VecComponentT(float _x, float _y, float _z, float _w)
				: x(_x), y(_y), z(_z), w(_w)
			{}
			// 一つ次元の少ないVectorで初期化.
			constexpr VecComponentT(const VecComponentT<3>& v, float _w)
				: x(v.x), y(v.y), z(v.z), w(_w)
			{}

			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(float _x, float _y, float _z, float _w, ComponentModifyFunc func)
				: x(func(_x)), y(func(_y)), z(func(_z)), w(func(_w))
			{}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecComponentT(const float* _data, ComponentModifyFunc func)
				: x(func(_data[0])), y(func(_data[1])), z(func(_data[2])), w(func(_data[3]))
			{}
		};

		// VectorN Template. N-> 2,3,4.
		template<int N>
		struct VecN : public VecComponentT<N>
		{
			static constexpr int DIMENSION = N;

			VecN() = default;
			constexpr VecN(const VecN& v) = default;

			explicit constexpr VecN(float v)
				: VecComponentT<N>(v)
			{
			}
			constexpr VecN(float _x, float _y)
				: VecComponentT<N>(_x, _y, 0, 0)
			{
			}
			constexpr VecN(float _x, float _y, float _z)
				: VecComponentT<N>(_x, _y, _z, 0)
			{
			}
			constexpr VecN(float _x, float _y, float _z, float _w)
				: VecComponentT<N>(_x, _y, _z, _w)
			{
			}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecN(float _x, float _y, float _z, float _w, ComponentModifyFunc func)
				: VecComponentT<N>(_x, _y, _z, _w, func)
			{
			}
			// 要素修正付きコンストラクタ.
			template<typename ComponentModifyFunc>
			constexpr VecN(const float* _data, ComponentModifyFunc func)
				: VecComponentT<N>(_data, func)
			{
			}

			// 1次元少ないvectorで初期化.
			constexpr VecN(const VecComponentT<N-1>& v, float s)
				: VecComponentT<N>(v, s)
			{
			}


			constexpr VecN<3> XYZ() const
			{
				return VecN<3>(this->x, this->y, this->z, 0);
			}
			constexpr VecN<2> XY() const
			{
				return VecN<2>(this->x, this->y, 0, 0);
			}

			bool operator ==(const VecN& v) const
			{
				return (0 == memcmp(this->data, v.data, sizeof(this->data)));
			}

			// += operator Broadcast.
			VecN& operator +=(const VecN& v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] += v.data[i];
				return *this;
			}
			// -= operator Broadcast.
			VecN& operator -=(const VecN& v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] -= v.data[i];
				return *this;
			}
			// *= operator Broadcast.
			VecN& operator *=(const VecN& v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] *= v.data[i];
				return *this;
			}
			// /= operator Broadcast.
			VecN& operator /=(const VecN& v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] /= v.data[i];
				return *this;
			}
			// *= operator Broadcast.
			VecN& operator *=(const float v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] *= v;
				return *this;
			}
			// /= operator Broadcast.
			VecN& operator /=(const float v)
			{
				for (auto i = 0; i < N; ++i)
					this->data[i] /= v;
				return *this;
			}

			float Length() const
			{
				return Length(*this);
			}

			static constexpr float Dot(const VecN& v0, const VecN& v1)
			{
				if constexpr (2 == DIMENSION)
					return v0.x * v1.x + v0.y * v1.y;
				else if constexpr (3 == DIMENSION)
					return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
				else if constexpr (4 == DIMENSION)
					return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
			}
			static constexpr VecN Cross(const VecN& v0, const VecN& v1)
			{
				if constexpr (3 == DIMENSION)
				{
					return VecN(
						v0.y * v1.z - v1.y * v0.z,
						v0.z * v1.x - v1.z * v0.x,
						v0.x * v1.y - v1.x * v0.y);
				}
			}
			static float LengthSq(const VecN& v)
			{
				return Dot(v, v);
			}
			static float Length(const VecN& v)
			{
				return std::sqrt(LengthSq(v));
			}

			static VecN Normalize(const VecN& v)
			{
				return v / v.Length();
			}

			static constexpr VecN Zero()
			{
				return VecN(0.0f);
			}
			static constexpr VecN One()
			{
				return VecN(1.0f);
			}
			static constexpr VecN UnitX()
			{
				return VecN(1.0f, 0.0f, 0.0f, 0.0f);
			}
			static constexpr VecN UnitY()
			{
				return VecN(0.0f, 1.0f, 0.0f, 0.0f);
			}
			static constexpr VecN UnitZ()
			{
				return VecN(0.0f, 0.0f, 1.0f, 0.0f);
			}
			static constexpr VecN UnitW()
			{
				return VecN(0.0f, 0.0f, 0.0f, 1.0f);
			}
		};

		using Vec2 = VecN<2>;
		using Vec3 = VecN<3>;
		using Vec4 = VecN<4>;

		static constexpr auto k_sizeof_Vec2 = sizeof(Vec2);
		static constexpr auto k_sizeof_Vec3 = sizeof(Vec3);
		static constexpr auto k_sizeof_Vec4 = sizeof(Vec4);

		namespace
		{
			// Func(Vec, Vec)
			template<typename VecType, typename BinaryOpType>
			inline constexpr VecType VecTypeBinaryOp(const VecType& v0, const VecType& v1, BinaryOpType op)
			{
				if constexpr (2 == VecType::DIMENSION)
					return VecType(op(v0.x, v1.x), op(v0.y, v1.y));
				else if constexpr (3 == VecType::DIMENSION)
					return VecType(op(v0.x, v1.x), op(v0.y, v1.y), op(v0.z, v1.z));
				else if constexpr (4 == VecType::DIMENSION)
					return VecType(op(v0.x, v1.x), op(v0.y, v1.y), op(v0.z, v1.z), op(v0.w, v1.w));
			}

			// Func(Vec, float)
			template<typename VecType, typename BinaryOpType>
			inline constexpr VecType VecTypeBinaryOp(const VecType& v0, const float v1, BinaryOpType op)
			{
				if constexpr (2 == VecType::DIMENSION)
					return VecType(op(v0.x, v1), op(v0.y, v1));
				else if constexpr (3 == VecType::DIMENSION)
					return VecType(op(v0.x, v1), op(v0.y, v1), op(v0.z, v1));
				else if constexpr (4 == VecType::DIMENSION)
					return VecType(op(v0.x, v1), op(v0.y, v1), op(v0.z, v1), op(v0.w, v1));
			}
		}

		// -Vec
		template<typename VecType>
		inline constexpr VecType operator-(const VecType& v)
		{
			constexpr auto func = [](float e) {return -e; };

			if constexpr (2 == VecType::DIMENSION)
				return VecType(v.x, v.y, 0, 0, func);
			else if constexpr (3 == VecType::DIMENSION)
				return VecType(v.x, v.y, v.z, 0, func);
			else if constexpr (4 == VecType::DIMENSION)
				return VecType(v.x, v.y, v.z, v.w, func);
		}
		// Vec+Vec
		template<typename VecType>
		inline constexpr VecType operator+(const VecType& v0, const VecType& v1)
		{
			constexpr auto op = [](float e0, float e1) {return e0 + e1; };
			return VecTypeBinaryOp(v0, v1, op);
		}
		// Vec-Vec
		template<typename VecType>
		inline constexpr VecType operator-(const VecType& v0, const VecType& v1)
		{
			constexpr auto op = [](float e0, float e1) {return e0 - e1; };
			return VecTypeBinaryOp(v0, v1, op);
		}
		// Vec*Vec
		template<typename VecType>
		inline constexpr VecType operator*(const VecType& v0, const VecType& v1)
		{
			constexpr auto op = [](float e0, float e1) {return e0 * e1; };
			return VecTypeBinaryOp(v0, v1, op);
		}
		// Vec/Vec
		template<typename VecType>
		inline constexpr VecType operator/(const VecType& v0, const VecType& v1)
		{
			constexpr auto op = [](float e0, float e1) {return e0 / e1; };
			return VecTypeBinaryOp(v0, v1, op);
		}

		// Vec*float
		template<typename VecType>
		inline constexpr VecType operator*(const VecType& v0, const float v1)
		{
			constexpr auto op = [](float e0, float e1) {return e0 * e1; };
			return VecTypeBinaryOp(v0, v1, op);
		}
		// float*Vec
		template<typename VecType>
		inline constexpr VecType operator*(const float v0, const VecType& v1)
		{
			return v1 * v0;
		}
		// Vec/float
		template<typename VecType>
		inline constexpr VecType operator/(const VecType& v0, const float v1)
		{
			return v0 * (1.0f / v1);
		}
		// float/Vec
		template<typename VecType>
		inline constexpr VecType operator/(const float v0, const VecType& v1)
		{
			return VecType(v0) / v1;
		}
	}
}
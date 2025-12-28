#pragma once

#include <cmath>
#include <memory>

namespace ngl
{
    namespace math
    {
        namespace local {
            constexpr float abs(float x) {
                return x < 0 ? -x : x;
            }

            constexpr double abs(double x) {
                return x < 0 ? -x : x;
            }

            constexpr long double abs(long double x) {
                return x < 0 ? -x : x;
            }
        }


        template <typename T, int N>
        struct VecComponentT
        {
            template <typename TO_TYPE>
            constexpr VecComponentT<TO_TYPE,N> CastImpl() const
            {
                return VecComponentT<TO_TYPE, N>();
            }
        };

        // Vec2 Data.
        template <typename T>
        struct VecComponentT<T, 2>
        {
            using ComponentType = T;
            static constexpr int DIMENSION = 2;
            // data.
            union
            {
                struct
                {
                    T x, y;
                };
                T data[DIMENSION];
            };

            VecComponentT()                                 = default;
            constexpr VecComponentT(const VecComponentT& v) = default;
            explicit constexpr VecComponentT(T v)
                : x(v), y(v)
            {
            }
            constexpr VecComponentT(T _x, T _y, T _dummy0, T _dummy1)
                : x(_x), y(_y)
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(T _x, T _y, T _dummy0, T _dummy1, ComponentModifyFunc func)
                : x(func(_x)), y(func(_y))
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(const T* _data, ComponentModifyFunc func)
                : x(func(_data[0])), y(func(_data[1]))
            {
            }

            template <typename TO_TYPE>
            constexpr VecComponentT<TO_TYPE,DIMENSION> CastImpl() const
            {
                return VecComponentT<TO_TYPE, DIMENSION>(static_cast<TO_TYPE>(x), static_cast<TO_TYPE>(y), {}, {});
            }
        };

        // Vec3 Data.
        template <typename T>
        struct VecComponentT<T, 3>
        {
            using ComponentType = T;
            static constexpr int DIMENSION = 3;
            // data.
            union
            {
                struct
                {
                    T x, y, z;
                };
                T data[DIMENSION];
            };

            VecComponentT()                                 = default;
            constexpr VecComponentT(const VecComponentT& v) = default;
            explicit constexpr VecComponentT(T v)
                : x(v), y(v), z(v)
            {
            }
            constexpr VecComponentT(T _x, T _y, T _z, T _dummy0)
                : x(_x), y(_y), z(_z)
            {
            }
            // 一つ次元の少ないVectorで初期化.
            constexpr VecComponentT(const VecComponentT<T, 2>& v, T _z)
                : x(v.x), y(v.y), z(_z)
            {
            }

            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(T _x, T _y, T _z, T _dummy0, ComponentModifyFunc func)
                : x(func(_x)), y(func(_y)), z(func(_z))
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(const T* _data, ComponentModifyFunc func)
                : x(func(_data[0])), y(func(_data[1])), z(func(_data[2]))
            {
            }

            template <typename TO_TYPE>
            constexpr VecComponentT<TO_TYPE, DIMENSION> CastImpl() const
            {
                return VecComponentT<TO_TYPE, DIMENSION>(static_cast<TO_TYPE>(x), static_cast<TO_TYPE>(y), static_cast<TO_TYPE>(z), {});
            }
        };

        // Vec4 Data.
        template <typename T>
        struct VecComponentT<T, 4>
        {
            using ComponentType = T;
            static constexpr int DIMENSION = 4;
            // data.
            union
            {
                struct
                {
                    T x, y, z, w;
                };
                T data[DIMENSION];
            };

            VecComponentT()                                 = default;
            constexpr VecComponentT(const VecComponentT& v) = default;
            explicit constexpr VecComponentT(T v)
                : x(v), y(v), z(v), w(v)
            {
            }
            constexpr VecComponentT(T _x, T _y, T _z, T _w)
                : x(_x), y(_y), z(_z), w(_w)
            {
            }
            // 一つ次元の少ないVectorで初期化.
            constexpr VecComponentT(const VecComponentT<T, 3>& v, T _w)
                : x(v.x), y(v.y), z(v.z), w(_w)
            {
            }

            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(T _x, T _y, T _z, T _w, ComponentModifyFunc func)
                : x(func(_x)), y(func(_y)), z(func(_z)), w(func(_w))
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecComponentT(const T* _data, ComponentModifyFunc func)
                : x(func(_data[0])), y(func(_data[1])), z(func(_data[2])), w(func(_data[3]))
            {
            }

            template <typename TO_TYPE>
            constexpr VecComponentT<TO_TYPE, DIMENSION> CastImpl() const
            {
                return VecComponentT<TO_TYPE, DIMENSION>(static_cast<TO_TYPE>(x), static_cast<TO_TYPE>(y), static_cast<TO_TYPE>(z), static_cast<TO_TYPE>(w));
            }
        };

        // VectorN Template. N-> 2,3,4.
        template <typename T, int N>
        struct VecN : public VecComponentT<T, N>
        {
            using ComponentType = T;
            static constexpr int DIMENSION = N;

            VecN()                        = default;
            constexpr VecN(const VecN& v) = default;

            explicit constexpr VecN(T v)
                : VecComponentT<T, N>(v)
            {
            }
            constexpr VecN(T _x, T _y)
                : VecComponentT<T, N>(_x, _y, 0, 0)
            {
            }
            constexpr VecN(T _x, T _y, T _z)
                : VecComponentT<T, N>(_x, _y, _z, 0)
            {
            }
            constexpr VecN(T _x, T _y, T _z, T _w)
                : VecComponentT<T, N>(_x, _y, _z, _w)
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecN(T _x, T _y, T _z, T _w, ComponentModifyFunc func)
                : VecComponentT<T, N>(_x, _y, _z, _w, func)
            {
            }
            // 要素修正付きコンストラクタ.
            template <typename ComponentModifyFunc>
            constexpr VecN(const T* _data, ComponentModifyFunc func)
                : VecComponentT<T, N>(_data, func)
            {
            }

            constexpr VecN(const VecComponentT<T, N>& v)
                : VecComponentT<T, N>(v)
            {
            }
            // 1次元少ないvectorで初期化.
            constexpr VecN(const VecComponentT<T, N - 1>& v, T s)
                : VecComponentT<T, N>(v, s)
            {
            }


            template <typename TO_TYPE>
            constexpr VecN<TO_TYPE, DIMENSION> Cast() const
            {
                return this->CastImpl<TO_TYPE>();
                //return VecComponentT<TO_TYPE, DIMENSION>(static_cast<TO_TYPE>(this->x), static_cast<TO_TYPE>(this->y), static_cast<TO_TYPE>(this->z), static_cast<TO_TYPE>(this->w));
            }


            constexpr VecN<T, 3> XYZ() const
            {
                return VecN<T, 3>(this->x, this->y, this->z, 0);
            }
            constexpr VecN<T, 2> XY() const
            {
                return VecN<T, 2>(this->x, this->y, 0, 0);
            }
            // インデックスアクセスオペレータ.
            constexpr T Component(int index) const
            {
                return this->data[index];
            }
            // インデックスアクセスオペレータ.
            constexpr T& Component(int index)
            {
                return this->data[index];
            }

            bool operator==(const VecN& v) const
            {
                return (0 == memcmp(this->data, v.data, sizeof(this->data)));
            }

            // += operator Broadcast.
            VecN& operator+=(const VecN& v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] += v.data[i];
                return *this;
            }
            // -= operator Broadcast.
            VecN& operator-=(const VecN& v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] -= v.data[i];
                return *this;
            }
            // *= operator Broadcast.
            VecN& operator*=(const VecN& v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] *= v.data[i];
                return *this;
            }
            // /= operator Broadcast.
            VecN& operator/=(const VecN& v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] /= v.data[i];
                return *this;
            }
            // *= operator Broadcast.
            VecN& operator*=(const T v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] *= v;
                return *this;
            }
            // /= operator Broadcast.
            VecN& operator/=(const T v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] /= v;
                return *this;
            }
            // %= operator Broadcast.
            VecN& operator%=(const T v)
            {
                for (auto i = 0; i < N; ++i)
                    this->data[i] %= v;
                return *this;
            }

            T Length() const
            {
                return Length(*this);
            }

            static constexpr T Dot(const VecN& v0, const VecN& v1)
            {
                if constexpr (2 == DIMENSION)
                    return v0.x * v1.x + v0.y * v1.y;
                else if constexpr (3 == DIMENSION)
                    return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
                else if constexpr (4 == DIMENSION)
                    return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
                else
                    static_assert("Dot is only defined in 2D, 3D and 4D.");
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
                static_assert(3 == DIMENSION, "Cross is only defined in 3D.");
            }
            static T LengthSq(const VecN& v)
            {
                return Dot(v, v);
            }
            static T Length(const VecN& v)
            {
                return std::sqrt(LengthSq(v));
            }

            static VecN Normalize(const VecN& v)
            {
                return v / v.Length();
            }

            static constexpr VecN Zero()
            {
                return VecN(static_cast<T>(0));
            }
            static constexpr VecN One()
            {
                return VecN(static_cast<T>(1));
            }
            static constexpr VecN UnitX()
            {
                return VecN(static_cast<T>(1), static_cast<T>(0), static_cast<T>(0), static_cast<T>(0));
            }
            static constexpr VecN UnitY()
            {
                return VecN(static_cast<T>(0), static_cast<T>(1), static_cast<T>(0), static_cast<T>(0));
            }
            static constexpr VecN UnitZ()
            {
                return VecN(static_cast<T>(0), static_cast<T>(0), static_cast<T>(1), static_cast<T>(0));
            }
            static constexpr VecN UnitW()
            {
                return VecN(static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), static_cast<T>(1));
            }

            static constexpr VecN Floor(const VecN& v)
            {
                constexpr auto func = [](T e)
                { return std::floor(e); };
                return VecN(v.data, func);
            }
            static constexpr VecN Ceil(const VecN& v)
            {
                constexpr auto func = [](T e)
                { return std::ceil(e); };
                return VecN(v.data, func);
            }
            static constexpr VecN Abs(const VecN& v)
            {
                constexpr auto func = [](T e)
                { return local::abs(e); };
                return VecN(v.data, func);
            }
        };

        using Vec2 = VecN<float, 2>;
        using Vec3 = VecN<float, 3>;
        using Vec4 = VecN<float, 4>;
        using Vec2d = VecN<double, 2>;
        using Vec3d = VecN<double, 3>;
        using Vec4d = VecN<double, 4>;
        using Vec2i = VecN<int, 2>;
        using Vec3i = VecN<int, 3>;
        using Vec4i = VecN<int, 4>;
        using Vec2u = VecN<unsigned int, 2>;
        using Vec3u = VecN<unsigned int, 3>;
        using Vec4u = VecN<unsigned int, 4>;

        static constexpr auto k_sizeof_Vec2 = sizeof(Vec2);
        static constexpr auto k_sizeof_Vec3 = sizeof(Vec3);
        static constexpr auto k_sizeof_Vec4 = sizeof(Vec4);

        namespace
        {
            // Func(Vec, Vec)
            template <typename VecType, typename BinaryOpType>
            inline constexpr VecType VecTypeBinaryOp(const VecType& v0, const VecType& v1, BinaryOpType op)
            {
                if constexpr (2 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1.x), op(v0.y, v1.y));
                else if constexpr (3 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1.x), op(v0.y, v1.y), op(v0.z, v1.z));
                else if constexpr (4 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1.x), op(v0.y, v1.y), op(v0.z, v1.z), op(v0.w, v1.w));
            }

            // Func(Vec, scalar)
            template <typename VecType, typename BinaryOpType>
            inline constexpr VecType VecTypeBinaryOp(const VecType& v0, const typename VecType::ComponentType v1, BinaryOpType op)
            {
                if constexpr (2 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1), op(v0.y, v1));
                else if constexpr (3 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1), op(v0.y, v1), op(v0.z, v1));
                else if constexpr (4 == VecType::DIMENSION)
                    return VecType(op(v0.x, v1), op(v0.y, v1), op(v0.z, v1), op(v0.w, v1));
            }
        }  // namespace

        // -Vec
        template <typename VecType>
        inline constexpr VecType operator-(const VecType& v)
        {
            constexpr auto func = [](typename VecType::ComponentType e)
            { return -e; };

            if constexpr (2 == VecType::DIMENSION)
                return VecType(v.x, v.y, 0, 0, func);
            else if constexpr (3 == VecType::DIMENSION)
                return VecType(v.x, v.y, v.z, 0, func);
            else if constexpr (4 == VecType::DIMENSION)
                return VecType(v.x, v.y, v.z, v.w, func);
        }
        // Vec+Vec
        template <typename VecType>
        inline constexpr VecType operator+(const VecType& v0, const VecType& v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 + e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }
        // Vec-Vec
        template <typename VecType>
        inline constexpr VecType operator-(const VecType& v0, const VecType& v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 - e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }
        // Vec*Vec
        template <typename VecType>
        inline constexpr VecType operator*(const VecType& v0, const VecType& v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 * e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }
        // Vec/Vec
        template <typename VecType>
        inline constexpr VecType operator/(const VecType& v0, const VecType& v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 / e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }
        // Vec%Vec
        template <typename VecType>
        inline constexpr VecType operator%(const VecType& v0, const VecType& v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 % e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }

        // Vec*scalar
        template <typename VecType>
        inline constexpr VecType operator*(const VecType& v0, const typename VecType::ComponentType v1)
        {
            constexpr auto op = [](typename VecType::ComponentType e0, typename VecType::ComponentType e1)
            { return e0 * e1; };
            return VecTypeBinaryOp(v0, v1, op);
        }
        // scalar*Vec
        template <typename VecType>
        inline constexpr VecType operator*(const typename VecType::ComponentType v0, const VecType& v1)
        {
            return v1 * v0;
        }
        // Vec/scalar
        template <typename VecType>
        inline constexpr VecType operator/(const VecType& v0, const typename VecType::ComponentType v1)
        {
            return v0 * ( static_cast<typename VecType::ComponentType>(1) / v1);
        }
        // scalar/Vec
        template <typename VecType>
        inline constexpr VecType operator/(const typename VecType::ComponentType v0, const VecType& v1)
        {
            return VecType(v0) / v1;
        }
        // Vec%scalar
        template <typename VecType>
        inline constexpr VecType operator%(const VecType& v0, const typename VecType::ComponentType v1)
        {
            return VecType(v0.x % v1, v0.y % v1, v0.z % v1);
        }

    }  // namespace math
}  // namespace ngl
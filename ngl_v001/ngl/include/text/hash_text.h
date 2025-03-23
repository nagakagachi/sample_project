#pragma once

#ifndef _NGL_UTIL_HASH_TEXT_
#define _NGL_UTIL_HASH_TEXT_

#include <algorithm>
#include <cassert>
#include <stdarg.h>

/*
	文字列とそのhash値を保持し、等価演算を高速化する
	templateによる固定長

	サイズ違いのインスタンス同士の代入などは今後追加予定
*/


namespace ngl
{
	namespace text
	{
		// FNV-1a UTILITIES
		// These functions are extremely performance sensitive, check examples like
		// that in VSO-653642 before making changes.
#if defined(_WIN64)
		constexpr size_t NGL_FNV_offset_basis = 14695981039346656037ULL;
		constexpr size_t NGL_FNV_prime = 1099511628211ULL;
#else // defined(_WIN64)
		constexpr size_t NGL_FNV_offset_basis = 2166136261U;
		constexpr size_t NGL_FNV_prime = 16777619U;
#endif // defined(_WIN64)

#if 0
		// 配列から Fnv1aハッシュを計算
		// accumulate range [_First, _First + _Count) into partial FNV-1a hash _Val
		inline size_t Fnv1a_append_bytes(size_t _Val, const unsigned char* const _First, const size_t _Count) noexcept
		{
			for (size_t _Idx = 0; _Idx < _Count; ++_Idx) {
				_Val ^= static_cast<size_t>(_First[_Idx]);
				_Val *= NGL_FNV_prime;
			}

			return _Val;
		}
		// 配列から Fnv1aハッシュを計算
		// FUNCTION TEMPLATE _Hash_array_representation
		// bitwise hashes the representation of an array
		template <class _Kty>
		size_t Fnv1a_Hash_array_representation(
			const _Kty* const _First, const size_t _Count) noexcept
		{
			return Fnv1a_append_bytes(NGL_FNV_offset_basis, reinterpret_cast<const unsigned char*>(_First), _Count * sizeof(_Kty));
		}
#endif

		// 配列から Fnv1aハッシュを計算
		// accumulate range [_First, _First + _Count) into partial FNV-1a hash _Val
		inline constexpr size_t Fnv1a_append_bytes_char(size_t _Val, const char* const _First, const size_t _Count) noexcept
		{
			for (size_t _Idx = 0; _Idx < _Count; ++_Idx) {
				_Val ^= static_cast<size_t>(_First[_Idx]);
				_Val *= NGL_FNV_prime;
			}

			return _Val;
		}
		// 配列から Fnv1aハッシュを計算
		// FUNCTION TEMPLATE _Hash_array_representation
		// bitwise hashes the representation of an array
		constexpr size_t Fnv1a_Hash_array_representation_char(
			const char* const _First, const size_t _Count) noexcept
		{
			return Fnv1a_append_bytes_char(NGL_FNV_offset_basis, _First, _Count * sizeof(char));
		}





		template<int LEN>
		class HashText
		{
		private:
			static constexpr int StrLenConstexpr(const char* str)
			{
				int l = 0;
				for (; 0 != str[l]; ++l)
				{
				}
				return l;
			}

		public:
			constexpr HashText()
				: str_()
				, valid_len_()
				, hash_()
			{
			}
			constexpr HashText(const char* s)
				: str_()
				, valid_len_()
				, hash_()
			{
				const int N = StrLenConstexpr(s);
				valid_len_ = (LEN < N) ? LEN : N;
				str_[valid_len_] = 0;
				for (auto i = 0; i < valid_len_; ++i)
					str_[i] = s[i];

				// Hash.
				hash_ = ngl::text::Fnv1a_Hash_array_representation_char(str_, valid_len_);
			}
			void Set(const char* s, int len)
			{
				const int N = len;
				valid_len_ = (LEN < N) ? LEN : N;

				for (auto i = 0; i < valid_len_; ++i)
					str_[i] = s[i];
				for (auto i = valid_len_; i < LEN; ++i)
					str_[i] = 0;

				// Hash.
				hash_ = ngl::text::Fnv1a_Hash_array_representation_char(str_, valid_len_);
			}
			constexpr const char* Get() const
			{
				return &str_[0];
			}
			constexpr uint64_t Hash() const
			{
				return hash_;
			}
			constexpr int Length() const
			{
				return valid_len_;
			}
			constexpr int MaxLength() const
			{
				return LEN;
			}

		private:
			char str_[LEN + 1] = {};
			int  valid_len_ = {};
			uint64_t hash_ = {};
		};
		namespace
		{
			template<int N0, int N1>
			struct CompStr
			{
				constexpr CompStr(const HashText<N0>& v0, const HashText<N1>& v1)
					: result()
				{
					result = true;
					const int n = (v0.Length() < v1.Length()) ? v0.Length() : v1.Length();
					for (int i = 0; i < n; ++i)
					{
						result &= (v0.Get()[i] == v1.Get()[i]);
					}
				}
				bool result;
			};
		}
		template<int LEN0, int LEN1>
		static constexpr bool operator == (const HashText<LEN0>& v0, const HashText<LEN1>& v1)
		{
			bool result = (v0.Hash() == v1.Hash()) && CompStr(v0, v1).result;
			return result;
		}
		template<int LEN0, int LEN1>
		static constexpr bool operator != (const HashText<LEN0>& v0, const HashText<LEN1>& v1)
		{
			return !(v0 == v1);
		}



		/*
			バッファの末尾が常に0で埋められていることを保証する固定長文字列.
		*/
		template<unsigned int SIZE>
		class FixedString
		{
			static constexpr unsigned int k_size = SIZE;
			static constexpr unsigned int k_buffer_size = k_size + 1;
			
		public:
			FixedString()
			{
#ifdef _DEBUG
				// デバッグ用に不正な初期値書き込み
				memset(text_, 0xcd, std::size(text_));
#endif

				valid_len_ = 0;
				std::memset(text_, 0, std::size(text_));
			}
			~FixedString()
			{
			}

			FixedString(const char* str)
			{
				Set(str, std::min(static_cast<unsigned int>(std::strlen(str)), k_size));
			}
			FixedString(const FixedString& obj)
			{
				Set(obj.text_, obj.valid_len_);
			}
			FixedString(const char* format, ...)
			{
				// ここで全て展開.
				char buf[k_buffer_size];
				va_list args;
				va_start( args, format );
				const auto n = vsnprintf( buf, sizeof(buf), format, args );
				va_end( args );
				
				Set(buf, n);
			}

			FixedString& operator = (const char* str)
			{
				Set(str, std::min(static_cast<unsigned int>(std::strlen(str)), k_size));
				return *this;
			}
			FixedString& operator = (const FixedString& obj)
			{
				Set(obj.text_, obj.valid_len_);
				return *this;
			}

			bool operator == (const FixedString& obj) const
			{
				if (obj.valid_len_ != valid_len_)
					return false;
				return 0 == std::memcmp(text_, obj.text_, valid_len_);
			}

			operator const char*() const
			{
				return Get();
			}

			void Set(const char* str, unsigned int size)
			{
				// このメソッドの呼び出しは必ず適正なsizeが渡される
				assert(k_size >= size);

				std::copy_n(str, size, text_);
				// 有効文字列長より後ろのバッファは必ずゼロで埋まっていることを保証.
				if (valid_len_ > size)
				{
					// 既存の有効文字列長より短い場合は差分をゼロ埋め
					std::memset(text_ + size, 0, valid_len_ - size);
				}
				// 有効文字列長更新
				valid_len_ = size;
			}

			const char* Get() const
			{
				return text_;
			}
			unsigned int GetValidLen() const
			{
				return valid_len_;
			}


			constexpr unsigned int GetMaxLen() const { return k_size; }
			static constexpr unsigned int MaxLength() { return k_size; }
		private:

		private:
			unsigned int		valid_len_ = 0;
			char				text_[k_buffer_size] = {};
		};

	}
}

namespace std
{
	template <unsigned int SIZE>
	struct hash<ngl::text::HashText<SIZE>>
	{
		size_t operator()(const ngl::text::HashText<SIZE>& v) const
		{
			return v.Hash();
		}
	};
}

namespace std
{
	template <unsigned int SIZE>
	struct hash<ngl::text::FixedString<SIZE>>
	{
		size_t operator()(const ngl::text::FixedString<SIZE>& v) const
		{
			return ngl::text::Fnv1a_Hash_array_representation_char(v.Get(), v.GetValidLen());
		}
	};
}

#include "hash_text.inl"

#endif // _NGL_UTIL_HASH_TEXT_
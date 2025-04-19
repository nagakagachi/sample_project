#pragma once

#include <atomic>

namespace ngl
{
namespace thread
{
	/// @brief 軽量なロックフリースタック実装
	/// @details プロデューサー/コンシューマーパターン向けに最適化された軽量なロックフリースタック。
	/// 主な特徴:
	/// - イントルーシブ設計により、メモリ効率が良く、余分なメモリ確保が不要
	/// - シンプルな実装で軽量な処理を実現
	/// 
	/// 注意点:
	/// - ABA問題への対策は実装していないため、PopしたポインタをそのままPushし直す場合は問題が発生する可能性あり
	/// - 一般的なプロデューサー/コンシューマーパターンでの使用を推奨
	/// @tparam T Node構造体を継承したクラス
	template<typename T>
	class LockFreeStackIntrusive
	{
	public:
		struct Node
		{
			Node() {}

			virtual ~Node() {};

			T& Get() 
			{ 
				return *static_cast<T*>(this); 
			}
			const T& Get() const
			{
				return *static_cast<T*>(this);
			}

			Node(const Node& o)
			{
				// Nothing. atomicはコピー禁止のためコピーコンストラクタ/代入演算子を明示定義しないと定義自体がdeleteされる.
			}
			Node& operator=(const Node & o)
			{
				// Nothing. atomicはコピー禁止のためコピーコンストラクタ/代入演算子を明示定義しないと定義自体がdeleteされる.
				return *this;
			}

		private:
			friend class LockFreeStackIntrusive;

			std::atomic<T*> next = nullptr;
		};


	public:
		LockFreeStackIntrusive()
		{
		}
		void Push(T* node)
		{
			// メモリオーダリングの説明:
			// - load/store には memory_order_relaxed を使用：この時点での順序付けは不要
			// - compare_exchange_weak の成功時は memory_order_release：
			//   このPush操作以降の操作が、この時点より前に行われた操作の結果を確実に見ることを保証
			// - 失敗時は memory_order_relaxed：再試行されるため強い保証は不要
			while (true)
			{
				T* old = top_.load(std::memory_order_relaxed);
				node->next.store(old, std::memory_order_relaxed);
				if (top_.compare_exchange_weak(old, node,
					std::memory_order_release,
					std::memory_order_relaxed))
				{
					break;
				}
			}
		}
		// ABA問題への対策はしていないため, PopしたポインタをそのままPushし直すようなタスクでは問題が発生する可能性がある.
		T* Pop()
		{
			// メモリオーダリングの説明:
			// - top_.load() に memory_order_acquire を使用：
			//   この操作より前のすべての変更（特にPushでの変更）が確実に見えることを保証
			// - next.load() は memory_order_relaxed：CAS操作で保護されるため弱い保証で十分
			// - compare_exchange_weak は両方のケースで memory_order_acquire：
			//   Pop操作の一貫性を保証し、それ以前のすべての変更が見えることを保証
			T* old = nullptr;
			while (true)
			{
				old = top_.load(std::memory_order_acquire);
				if (nullptr == old)
					break;

				T* next = old->next.load(std::memory_order_relaxed);
				if (top_.compare_exchange_weak(old, next,
					std::memory_order_acquire,
					std::memory_order_acquire))
				{
					break;
				}
			}

			return old;
		}
	private:
		std::atomic<T*> top_ = nullptr;
	};
}
}


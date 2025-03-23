
#include "thread/lockfree_stack_intrusive_test.h"

#include <thread>
#include <vector>
#include <algorithm>
#include <iostream>

#include <assert.h>
#include <Windows.h>

namespace ngl
{
namespace thread
{
namespace test
{

	using TestStack = ngl::thread::LockFreeStackIntrusive<struct TestStackNode>;

	struct TestStackNode : public TestStack::Node
	{
		TestStackNode(int v)
		{
			data = v;
			enq_id = 0;
			deq_id = 0;
		}
		int data;
		int enq_id;
		int deq_id;
	};

	class th_push_functor
	{
	public:
		TestStack* dst_ = nullptr;
		int id_ = 0;

		int start_index_ = 0;
		int push_count_ = 0;

		th_push_functor(TestStack* p, int id, int start_index, int push_count)
		{
			dst_ = p;
			id_ = id;

			start_index_ = start_index;
			push_count_ = push_count;
		}

		void operator()()
		{
			for (auto i = 0; i < push_count_; ++i)
			{
				//Sleep(1);

				auto* n = new TestStackNode(start_index_ + i);
				n->enq_id = id_;

				dst_->Push(n);
			}
		}
	};

	class th_pop_functor
	{
	public:
		TestStack* src_ = nullptr;
		TestStack* dst_ = nullptr;
		int id_ = 0;

		th_pop_functor(TestStack* p_src, TestStack* p_dst, int id)
		{
			src_ = p_src;
			dst_ = p_dst;
			id_ = id;
		}

		void operator()()
		{
			Sleep(1);
			while (auto* n = src_->Pop())
			{
				//Sleep(1);

				// Popした要素を別のStackへPush.
				n->deq_id = id_;
				dst_->Push(n);
			}
		}
	};

	void LockfreeStackIntrusiveTest()
	{
		// LockfreeStackIntrusiveのテスト.
		//	マルチスレッドで1つ目のStackへPush.
		//	同時にマルチスレッドで1つ目のStackからPopして2つ目のStackへPush.


		TestStack stack0 = {};
		TestStack stack1 = {};

		std::vector<std::thread*> thread_array;
		constexpr int k_per_thread_element_count = 100;
		constexpr int k_push_thread_count = 16;
		constexpr int k_pop_thread_count = 4;
		int push_counter = 0;
		int pop_counter = 0;
		for (int i = 0; i < k_push_thread_count; ++i)
		{
			// 連番数字をPushすることでエラーがないかチェック.
			thread_array.push_back(new std::thread(th_push_functor(&stack0, push_counter++, i * k_per_thread_element_count, k_per_thread_element_count)));
		}
		for (int i = 0; i < k_pop_thread_count; ++i)
		{
			thread_array.push_back(new std::thread(th_pop_functor(&stack0, &stack1, pop_counter++)));
		}

		// 待機.
		for (auto& e : thread_array)
			e->join();

		// 同期後に残った分を処理.
		th_pop_functor(&stack0, &stack1, pop_counter++)();


		// -----------------------------------------------------------------------------
		// 検証.

		// 同期後の追加処理で全て処理しているはずなのでSrcには残っていない.
		auto* src_pop = stack0.Pop();
		assert(nullptr == src_pop);

		int valid_deq_count = 0;
		std::vector<std::tuple<int, int, int>> validation_array;
		while (auto* n = stack1.Pop())
		{
			// 内容チェック.
			validation_array.push_back(std::make_tuple(n->data, n->enq_id, n->deq_id));

			delete n;

			++valid_deq_count;
		}
		// ソート.
		std::sort(validation_array.begin(), validation_array.end(),
			[](const auto& e0, const auto& e1) {
				return std::get<0>(e0) < std::get<0>(e1);
			});

		for (int i = 0; i < (int)validation_array.size() - 1; ++i)
		{
			auto e0 = std::get<0>(validation_array[i]);
			auto e1 = std::get<0>(validation_array[i + 1]);

			if (e1 != (e0 + 1))
			{
				// 全ての要素が重複なし連番のはずなのでそうでない場合はプログラム側に問題がある.
				assert(false);
			}
		}

		// 片付け.
		for (auto& e : thread_array)
			delete e;
		thread_array.clear();

		std::cout << "Test End LockFreeStackIntrusive" << std::endl;

	}
}
}
}
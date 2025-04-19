#include "thread/test_lockfree_stack.h"
#include "thread/lockfree_stack_intrusive.h"
#include "thread/lockfree_stack_fixed_size.h"
#include "thread/lockfree_stack_static_size.h"

#include <thread>
#include <vector>
#include <algorithm>
#include <iostream>
#include <optional>

namespace ngl {
namespace thread {

    // テスト用のノードクラス
    using TestStack = ngl::thread::LockFreeStackIntrusive<struct TestStackNode>;
    
    struct TestStackNode : public TestStack::Node 
    {
        TestStackNode(int v) : value(v), push_thread_id(0), pop_thread_id(0) {}
        int value;
        int push_thread_id;
        int pop_thread_id;
    };

    // プッシュ操作を行うスレッド関数
    class TestPushWorker {
    public:
        TestStack* stack = nullptr;
        int thread_id = 0;
        int start_value = 0;
        int num_pushes = 0;

        TestPushWorker(TestStack* s, int id, int start, int count) 
            : stack(s), thread_id(id), start_value(start), num_pushes(count) {}

        void operator()() {
            for (int i = 0; i < num_pushes; ++i) {
                auto* node = new TestStackNode(start_value + i);
                node->push_thread_id = thread_id;
                stack->Push(node);
            }
        }
    };

    // ポップ操作を行うスレッド関数
    class TestPopWorker {
    public:
        TestStack* src_stack = nullptr;
        TestStack* dst_stack = nullptr;
        int thread_id = 0;

        TestPopWorker(TestStack* src, TestStack* dst, int id) 
            : src_stack(src), dst_stack(dst), thread_id(id) {}

        void operator()() {
            while (auto* node = src_stack->Pop()) {
                node->pop_thread_id = thread_id;
                dst_stack->Push(node);
            }
        }
    };

    void TestLockFreeStackIntrusive()
    {
        std::cout << "Starting LockFreeStack Test..." << std::endl;

        TestStack stack1;  // プッシュ用スタック
        TestStack stack2;  // ポップ後の要素保存用スタック

        std::vector<std::thread*> threads;

        // テストパラメータ
        const int items_per_thread = 1000;
        const int num_push_threads = 4;
        const int num_pop_threads = 2;
        
        // プッシュスレッドの作成と開始
        for (int i = 0; i < num_push_threads; ++i) {
            threads.push_back(new std::thread(
                TestPushWorker(&stack1, i, i * items_per_thread, items_per_thread)
            ));
        }

        // ポップスレッドの作成と開始
        for (int i = 0; i < num_pop_threads; ++i) {
            threads.push_back(new std::thread(
                TestPopWorker(&stack1, &stack2, i)
            ));
        }

        // 全スレッドの終了を待機
        for (auto& t : threads) {
            t->join();
            delete t;
        }
        threads.clear();

        // 残りの要素をポップ
        TestPopWorker(&stack1, &stack2, num_pop_threads)();

        // 検証
        std::vector<TestStackNode*> result_nodes;
        while (auto* node = stack2.Pop()) {
            result_nodes.push_back(node);
        }

        // 期待される総要素数
        const int expected_total = num_push_threads * items_per_thread;
        const int actual_total = static_cast<int>(result_nodes.size());

        // 要素数の検証
        std::cout << "Expected items: " << expected_total << std::endl;
        std::cout << "Actual items: " << actual_total << std::endl;
        if (actual_total != expected_total) {
            std::cout << "ERROR: Item count mismatch!" << std::endl;
        }

        // 値の重複チェック
        std::sort(result_nodes.begin(), result_nodes.end(),
            [](const TestStackNode* a, const TestStackNode* b) {
                return a->value < b->value;
            });

        bool has_duplicates = false;
        for (size_t i = 1; i < result_nodes.size(); ++i) {
            if (result_nodes[i]->value == result_nodes[i-1]->value) {
                has_duplicates = true;
                std::cout << "ERROR: Duplicate value found: " << result_nodes[i]->value << std::endl;
            }
        }

        // メモリ解放
        for (auto* node : result_nodes) {
            delete node;
        }

        std::cout << "LockFreeStack Test " 
                  << (has_duplicates || actual_total != expected_total ? "FAILED" : "PASSED")
                  << std::endl;
    }

    template<typename Stack>
    void TestFixedSizeStackImpl(const char* test_name, Stack& stack)
    {
        std::cout << "Starting " << test_name << "..." << std::endl;

        constexpr int num_threads = 4;
        constexpr int ops_per_thread = 250; // スタックサイズに合わせて調整
        std::vector<int> value_counts(num_threads * ops_per_thread, 0);
        std::vector<std::thread> threads;

        // プッシュワーカー
        auto push_worker = [&stack](int thread_id, int start_val, int count) {
            for (int i = 0; i < count; ++i) {
                int value = start_val + i;
                int retry_count = 0;
                while (!stack.Push(value)) {
                    std::this_thread::yield();
                    if (++retry_count > 1000000) {
                        std::cout << "Push timeout for value " << value << std::endl;
                        return;
                    }
                }
            }
        };

        // ポップワーカー
        auto pop_worker = [&stack, &value_counts](int thread_id, int expected_count) {
            int popped_count = 0;
            int retry_count = 0;
            
            while (popped_count < expected_count) {
                auto val = stack.Pop();
                if (val) {
                    if (*val >= 0 && *val < static_cast<int>(value_counts.size())) {
                        ++value_counts[*val];
                    }
                    ++popped_count;
                    retry_count = 0;
                } else {
                    std::this_thread::yield();
                    if (++retry_count > 1000000) {
                        std::cout << "Pop timeout after " << popped_count << " items" << std::endl;
                        return;
                    }
                }
            }
        };

        // スタックが空であることを確認
        if (!stack.IsEmpty()) {
            std::cout << "ERROR: Stack is not empty at start" << std::endl;
            return;
        }

        // プッシュスレッドの開始
        std::vector<std::thread> push_threads;
        for (int i = 0; i < num_threads; ++i) {
            push_threads.emplace_back(push_worker, i, i * ops_per_thread, ops_per_thread);
        }

        // ポップスレッドの開始
        std::vector<std::thread> pop_threads;
        for (int i = 0; i < num_threads; ++i) {
            pop_threads.emplace_back(pop_worker, i, ops_per_thread);
        }

        // スレッドの終了待ち
        for (auto& t : push_threads) {
            t.join();
        }
        for (auto& t : pop_threads) {
            t.join();
        }

        // 結果の検証
        bool success = true;
        int total_ops = num_threads * ops_per_thread;
        
        // 各値が正確に1回だけ出現することを確認
        for (size_t i = 0; i < value_counts.size(); ++i) {
            if (value_counts[i] != 1) {
                std::cout << "ERROR: Value " << i << " was popped " 
                         << value_counts[i] << " times (expected: 1)" << std::endl;
                success = false;
            }
        }

        // スタックが空になっていることを確認
        if (!stack.IsEmpty()) {
            std::cout << "ERROR: Stack is not empty after test" << std::endl;
            success = false;
        }

        std::cout << test_name << " Test " << (success ? "PASSED" : "FAILED") << std::endl;
    }

    void TestFixedSizeLockFreeStack()
    {
        constexpr size_t stack_size = 1024;
        FixedSizeLockFreeStack<int> stack;
        if (!stack.Initialize(stack_size)) {
            std::cout << "Failed to initialize FixedSizeLockFreeStack" << std::endl;
            return;
        }
        TestFixedSizeStackImpl("FixedSizeLockFreeStack", stack);
    }

    void TestStaticSizeLockFreeStack()
    {
        StaticSizeLockFreeStack<int, 1024> stack; // 余裕を持ったサイズに増やす
        TestFixedSizeStackImpl("StaticSizeLockFreeStack", stack);
    }
    
} // namespace thread
} // namespace ngl
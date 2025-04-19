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

    /// @brief ロックフリースタックの並行処理テスト実装
    /// マルチスレッド環境での Push/Pop 操作の整合性を検証します。
    /// テストでは以下の項目を確認します：
    /// - 複数スレッドからの同時アクセスによるデータの整合性
    /// - 全ての要素が正しくPush/Popされること
    /// - 要素の重複や欠落が発生しないこと

    // テスト用のノードクラス
    using TestStack = ngl::thread::LockFreeStackIntrusive<struct TestStackNode>;
    
    /// @brief テスト用のノードクラス
    /// @details プッシュ/ポップを行ったスレッドのIDを追跡し、
    /// 処理の正当性を検証するために使用します
    struct TestStackNode : public TestStack::Node 
    {
        TestStackNode(int v) : value(v), push_thread_id(0), pop_thread_id(0) {}
        int value;              ///< ノードの値
        int push_thread_id;     ///< プッシュを行ったスレッドのID
        int pop_thread_id;      ///< ポップを行ったスレッドのID
    };

    /// @brief プッシュ操作のテストワーカークラス
    /// @details 指定された範囲の値を持つノードを生成し、
    /// スタックに連続的にプッシュする処理を実行します
    class TestPushWorker {
    public:
        TestStack* stack = nullptr;      ///< 操作対象のスタック
        int thread_id = 0;               ///< ワーカーのスレッドID
        int start_value = 0;             ///< プッシュする値の開始値
        int num_pushes = 0;              ///< プッシュする要素数

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

    /// @brief ポップ操作のテストワーカークラス
    /// @details ソーススタックからポップした要素を
    /// デスティネーションスタックにプッシュする処理を実行します。
    /// これにより、要素の移動を追跡し、データの整合性を検証できます。
    class TestPopWorker {
    public:
        TestStack* src_stack = nullptr;  ///< ポップ元のスタック
        TestStack* dst_stack = nullptr;  ///< プッシュ先のスタック
        int thread_id = 0;               ///< ワーカーのスレッドID

        TestPopWorker(TestStack* src, TestStack* dst, int id) 
            : src_stack(src), dst_stack(dst), thread_id(id) {}

        void operator()() {
            while (auto* node = src_stack->Pop()) {
                node->pop_thread_id = thread_id;
                dst_stack->Push(node);
            }
        }
    };

    /// @brief LockFreeStackIntrusiveクラスの並行処理テスト
    /// @details 複数のプッシュスレッドとポップスレッドを同時に実行し、
    /// データの整合性を検証します。以下の手順でテストを実施：
    /// 1. 複数のプッシュスレッドが異なる値範囲のノードをプッシュ
    /// 2. 複数のポップスレッドが同時にノードを取り出し
    /// 3. 全ての要素数、値の重複、欠落をチェック
    void TestLockFreeStackIntrusive()
    {
        std::cout << "Starting LockFreeStackIntrusive Test..." << std::endl;

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

        std::cout << "LockFreeStackIntrusive Test " 
                  << (has_duplicates || actual_total != expected_total ? "FAILED" : "PASSED")
                  << std::endl;
    }

    /// @brief 固定サイズロックフリースタックの並行処理テスト実装
    /// @details 固定サイズのスタックに対する並行アクセスのテストを実施します。
    /// このテストでは以下の項目を検証します：
    /// - 複数スレッドからの同時Push/Pop操作の整合性
    /// - スタックの容量制限の正常な動作
    /// - Push/Pop操作のタイムアウト処理
    /// - 値の重複や欠落が発生しないこと
    /// 
    /// テスト手順：
    /// 1. 複数のプッシュスレッドが同時に値を書き込み
    /// 2. 複数のポップスレッドが同時に値を読み出し
    /// 3. すべての値が1回だけ処理されることを確認
    /// 4. スタックが最終的に空になることを確認
    ///
    /// @tparam Stack 固定サイズスタックの型（FixedSizeLockFreeStackまたはStaticSizeLockFreeStack）
    /// @param test_name テストの識別名
    /// @param stack テスト対象のスタックインスタンス
    template<typename Stack>
    void TestFixedSizeStackImpl(const char* test_name, Stack& stack)
    {
        std::cout << "Starting " << test_name << "..." << std::endl;

        constexpr int num_threads = 4;
        constexpr int ops_per_thread = 250; // スタックサイズに合わせて調整
        std::vector<int> value_counts(num_threads * ops_per_thread, 0);
        std::vector<std::thread> threads;

        /// プッシュワーカー
        /// @details 指定された範囲の値をスタックにプッシュします。
        /// タイムアウト処理により、スタックが一杯の場合の待機と再試行を行います。
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

        /// ポップワーカー
        /// @details スタックから値をポップし、各値の出現回数を記録します。
        /// タイムアウト処理により、スタックが空の場合の待機と再試行を行います。
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
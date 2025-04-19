#include "thread/fixed_size_lock_free_stack.h"
#include <iostream>
#include <thread>
#include <vector>

namespace ngl {
namespace thread {
namespace {

void TestBasicOperations() {
    std::cout << "Testing basic operations..." << std::endl;
    FixedSizeLockFreeStack<int, 10> stack;
    
    assert(stack.IsEmpty() && "New stack should be empty");
    assert(!stack.IsFull() && "New stack should not be full");
    assert(stack.Capacity() == 10 && "Capacity should be 10");

    assert(stack.Push(1) && "Push should succeed");
    assert(stack.Push(2) && "Push should succeed");
    assert(stack.Push(3) && "Push should succeed");
    assert(!stack.IsEmpty() && "Stack should not be empty after push");

    {
        auto val = stack.Pop();
        assert(val && val.value() == 3 && "Should get last pushed value (3)");
    }
    {
        auto val = stack.Pop();
        assert(val && val.value() == 2 && "Should get second pushed value (2)");
    }
    {
        auto val = stack.Pop();
        assert(val && val.value() == 1 && "Should get first pushed value (1)");
    }
    
    assert(stack.IsEmpty() && "Stack should be empty after all pops");
    assert(!stack.Pop() && "Pop from empty stack should return nullopt");

    std::cout << "  Basic operations test passed" << std::endl;
}

void TestMultiThreaded() {
    std::cout << "Testing multi-threaded operations..." << std::endl;
    
    constexpr size_t NUM_THREADS = 4;
    constexpr size_t OPS_PER_THREAD = 10000;
    constexpr size_t STACK_SIZE = 100;

    FixedSizeLockFreeStack<int, STACK_SIZE> stack;
    constexpr auto bytesize_stack = sizeof(stack);
    std::atomic<int> push_success{0};
    std::atomic<int> pop_success{0};

    std::vector<std::thread> threads;
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&stack, &push_success, &pop_success]() {
            for (size_t j = 0; j < OPS_PER_THREAD; ++j) {
                if (stack.Push(static_cast<int>(j))) {
                    push_success++;
                }
                if (auto val = stack.Pop()) {
                    pop_success++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "  Successful pushes: " << push_success.load() << std::endl;
    std::cout << "  Successful pops: " << pop_success.load() << std::endl;
    
    assert(push_success.load() >= pop_success.load() && "Push count should be >= pop count");
    assert(stack.IsEmpty() || stack.Pop().has_value() && "Stack should be either empty or have values");

    std::cout << "  Multi-threaded test passed" << std::endl;
}

void TestFullStack() {
    std::cout << "Testing full stack operations..." << std::endl;

    constexpr size_t STACK_SIZE = 5;
    FixedSizeLockFreeStack<int, STACK_SIZE> stack;

    for (size_t i = 0; i < STACK_SIZE; ++i) {
        assert(stack.Push(static_cast<int>(i)) && "Push should succeed when not full");
    }

    assert(stack.IsFull() && "Stack should be full");
    assert(!stack.Push(100) && "Push to full stack should fail");

    for (size_t i = 0; i < STACK_SIZE; ++i) {
        auto val = stack.Pop();
        assert(val && val.value() == static_cast<int>(STACK_SIZE - 1 - i) && "Values should be popped in LIFO order");
    }

    assert(stack.IsEmpty() && "Stack should be empty after all pops");
    std::cout << "  Full stack test passed" << std::endl;
}

} // anonymous namespace

void TestCode() {
    std::cout << "=== Starting FixedSizeLockFreeStack tests ===" << std::endl;
    
    TestBasicOperations();
    TestMultiThreaded();
    TestFullStack();

    std::cout << "All tests passed!" << std::endl;
}

} // namespace thread
} // namespace ngl
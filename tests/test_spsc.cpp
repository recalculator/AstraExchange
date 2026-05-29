#include <gtest/gtest.h>
#include "utils/SpscRingBuffer.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>

using namespace astra;

// --- Single-threaded basic ops ---

TEST(SpscTest, PushPopSingleItem) {
    SpscRingBuffer<int> ring(4);
    EXPECT_TRUE(ring.push(42));
    auto v = ring.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(SpscTest, EmptyPopReturnsNullopt) {
    SpscRingBuffer<int> ring(4);
    EXPECT_FALSE(ring.pop().has_value());
}

TEST(SpscTest, FifoOrder) {
    SpscRingBuffer<int> ring(8);
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(ring.push(i));
    for (int i = 0; i < 5; ++i) {
        auto v = ring.pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
}

TEST(SpscTest, FullBufferRejectsPush) {
    // Capacity 4 means 3 usable slots (one slot is the sentinel gap).
    SpscRingBuffer<int> ring(4);
    // Ring has power-of-two capacity=4, mask=3; full when (tail+1)&3==head.
    // So we can push 3 items before the 4th push sees full.
    int pushed = 0;
    while (ring.push(pushed)) ++pushed;
    EXPECT_GT(pushed, 0);
    EXPECT_FALSE(ring.push(999)) << "Should be full after " << pushed << " items";
}

TEST(SpscTest, CapacityIsPowerOfTwo) {
    SpscRingBuffer<int> ring(5); // requested 5, should round up to 8
    EXPECT_EQ(ring.capacity(), 8u);
}

TEST(SpscTest, MoveOnlyType) {
    SpscRingBuffer<std::unique_ptr<int>> ring(4);
    ASSERT_TRUE(ring.push(std::make_unique<int>(99)));
    auto v = ring.pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_NE(*v, nullptr);
    EXPECT_EQ(**v, 99);
}

// --- Multi-threaded producer/consumer ---

TEST(SpscTest, MultiThreadedOneMillion) {
    const int N = 1'000'000;
    SpscRingBuffer<int> ring(1 << 14); // 16384 slots

    std::atomic<bool> done{false};
    std::vector<int>  received;
    received.reserve(N);

    std::thread consumer([&]() {
        int count = 0;
        while (count < N) {
            auto v = ring.pop();
            if (v) {
                received.push_back(*v);
                ++count;
            }
        }
        done = true;
    });

    for (int i = 0; i < N; ++i) {
        while (!ring.push(i)) {} // spin on backpressure
    }

    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), N);

    // Verify FIFO order
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], i) << "FIFO violation at index " << i;
        if (received[i] != i) break; // stop spamming on first failure
    }
}

TEST(SpscTest, ProducerConsumerSentinelPattern) {
    // Pattern used by pipeline mode: nullptr sentinel to signal end.
    SpscRingBuffer<std::shared_ptr<int>> ring(64);

    std::vector<int> received;

    std::thread consumer([&]() {
        while (true) {
            auto item = ring.pop();
            if (!item) continue;
            if (!*item) break; // nullptr = done
            received.push_back(**item);
        }
    });

    for (int i = 1; i <= 10; ++i) {
        auto p = std::make_shared<int>(i);
        while (!ring.push(p)) {}
    }
    // Push sentinel
    while (!ring.push(std::shared_ptr<int>{nullptr})) {}

    consumer.join();

    ASSERT_EQ(received.size(), 10u);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(received[i], i + 1);
}

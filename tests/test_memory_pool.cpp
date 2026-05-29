#include <gtest/gtest.h>
#include "utils/MemoryPool.hpp"
#include <string>
#include <vector>

using namespace astra;

// --- Basic allocation ---

TEST(MemoryPoolTest, AllocAndDealloc) {
    MemoryPool<int> pool(4);
    int* p = pool.alloc(42);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 42);
    EXPECT_EQ(pool.allocated(), 1u);
    pool.dealloc(p);
    EXPECT_EQ(pool.allocated(), 0u);
}

TEST(MemoryPoolTest, AllocFillsCapacity) {
    MemoryPool<int> pool(8);
    std::vector<int*> ptrs;
    for (int i = 0; i < 8; ++i) ptrs.push_back(pool.alloc(i));
    EXPECT_EQ(pool.allocated(), 8u);
    EXPECT_TRUE(pool.full());
    for (auto* p : ptrs) pool.dealloc(p);
    EXPECT_EQ(pool.allocated(), 0u);
}

TEST(MemoryPoolTest, ExhaustedThrowsBadAlloc) {
    MemoryPool<int> pool(2);
    pool.alloc(1);
    pool.alloc(2);
    EXPECT_THROW(pool.alloc(3), std::bad_alloc);
}

TEST(MemoryPoolTest, SlotIsReusedAfterDealloc) {
    MemoryPool<int> pool(1);
    int* p1 = pool.alloc(10);
    pool.dealloc(p1);
    // Pool is no longer full — we can allocate again from the same slot.
    EXPECT_FALSE(pool.full());
    int* p2 = pool.alloc(20);
    EXPECT_EQ(*p2, 20);
    pool.dealloc(p2);
}

TEST(MemoryPoolTest, PointersAreUnique) {
    MemoryPool<int> pool(16);
    std::vector<int*> ptrs;
    for (int i = 0; i < 16; ++i) ptrs.push_back(pool.alloc(i));
    // All pointers must be distinct
    for (size_t i = 0; i < ptrs.size(); ++i)
        for (size_t j = i + 1; j < ptrs.size(); ++j)
            EXPECT_NE(ptrs[i], ptrs[j]);
    for (auto* p : ptrs) pool.dealloc(p);
}

// --- Non-trivial type: string ---

TEST(MemoryPoolTest, NonTrivialTypeConstructsAndDestructs) {
    MemoryPool<std::string> pool(4);
    std::string* s = pool.alloc("hello");
    EXPECT_EQ(*s, "hello");
    pool.dealloc(s);
    EXPECT_EQ(pool.allocated(), 0u);
}

// --- Stress: allocate/free many times ---

TEST(MemoryPoolTest, AllocFreeRoundtrip) {
    MemoryPool<int> pool(64);
    for (int round = 0; round < 1000; ++round) {
        std::vector<int*> ptrs;
        for (int i = 0; i < 64; ++i) ptrs.push_back(pool.alloc(i));
        EXPECT_TRUE(pool.full());
        for (auto* p : ptrs) pool.dealloc(p);
        EXPECT_EQ(pool.allocated(), 0u);
    }
}

// --- Pool capacity/available accessors ---

TEST(MemoryPoolTest, AvailableDecreasesOnAlloc) {
    MemoryPool<int> pool(10);
    EXPECT_EQ(pool.available(), 10u);
    auto* p = pool.alloc(1);
    EXPECT_EQ(pool.available(), 9u);
    pool.dealloc(p);
    EXPECT_EQ(pool.available(), 10u);
}

// --- Null dealloc is safe ---

TEST(MemoryPoolTest, DeallocNullIsNoop) {
    MemoryPool<int> pool(4);
    EXPECT_NO_THROW(pool.dealloc(nullptr));
}

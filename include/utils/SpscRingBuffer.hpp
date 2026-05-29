#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <new>

namespace astra {

// Single-Producer / Single-Consumer lock-free ring buffer.
//
// Capacity must be a power of two. Internally holds capacity+1 slots so
// that full and empty are distinguishable without a separate counter.
// (We track head/tail as raw indices and mask with capacity_mask_.)
//
// Memory ordering:
//   - Producer: stores item, then releases tail_ with release semantics so
//     the consumer sees the write.
//   - Consumer: acquires tail_ before reading item, releases head_ after
//     consuming so the producer sees the freed slot.
//
// NOT safe for multiple producers or multiple consumers.
// Safe for T that is trivially copyable or move-constructible.

template<typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity)
        : capacity_(nextPow2(capacity))
        , mask_(capacity_ - 1)
        , slots_(new Slot[capacity_])
        , head_(0)
        , tail_(0)
    {
        static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>);
    }

    ~SpscRingBuffer() = default;

    SpscRingBuffer(const SpscRingBuffer&)            = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Producer side. Returns false if buffer is full (backpressure).
    bool push(T item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & mask_;

        // Full if next == head
        if (next == head_.load(std::memory_order_acquire)) return false;

        new (&slots_[tail].storage) T(std::move(item));
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns nullopt if buffer is empty.
    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const size_t head = head_.load(std::memory_order_relaxed);

        // Empty if head == tail
        if (head == tail_.load(std::memory_order_acquire)) return std::nullopt;

        T* ptr = reinterpret_cast<T*>(&slots_[head].storage);
        T item = std::move(*ptr);
        ptr->~T();

        head_.store((head + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool   empty()    const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }
    size_t capacity() const noexcept { return capacity_; }

    // Approximate size — only safe to call from one side at a time.
    size_t approxSize() const noexcept {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_relaxed);
        return (t - h + capacity_) & mask_;
    }

private:
    static size_t nextPow2(size_t n) {
        // Minimum size 2 so mask is at least 1.
        if (n < 2) return 2;
        size_t p = 1;
        while (p < n) p <<= 1;
        // Add 1 so we can distinguish full from empty:
        // we use capacity_ slots with the (tail+1)==head sentinel.
        // Actually we need capacity_ to BE a power of two and use the
        // wrap-around naturally, so return p (not p+1).
        return p;
    }

    // Pad head and tail to their own cache lines to avoid false sharing.
    struct alignas(64) PaddedAtomic {
        std::atomic<size_t> value{0};
        std::byte pad[64 - sizeof(std::atomic<size_t>)];
    };

    struct alignas(alignof(T)) Slot {
        std::byte storage[sizeof(T)];
    };

    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<Slot[]> slots_;

    // Head is updated by consumer; tail by producer.
    // Separate cache lines prevent false sharing between threads.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace astra

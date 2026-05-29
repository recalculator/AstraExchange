#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <new>
#include <stdexcept>
#include <vector>

namespace astra {

// Fixed-capacity slab allocator for objects of type T.
//
// Design:
//   - Pre-allocates `capacity` raw slots at construction. No per-object
//     heap calls during alloc/dealloc.
//   - Free slots are tracked with an explicit index-based freelist stored in
//     a separate vector<uint32_t>. This avoids any minimum size constraint on T
//     (unlike pointer-overlay freelists that require sizeof(T)>=sizeof(void*)).
//   - alloc/dealloc are O(1).
//   - NOT thread-safe. Single-threaded use only — matches the matching engine
//     single-threaded processing model.
//   - Objects are constructed in-place (placement new) and explicitly
//     destroyed on dealloc.
//
// Usage:
//   MemoryPool<Order> pool(1'000'000);
//   Order* p = pool.alloc(args...);   // construct in pool slot
//   pool.dealloc(p);                   // destroy + return slot

template<typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t capacity)
        : capacity_(capacity)
        , allocated_(0)
    {
        // Raw aligned storage for `capacity` T objects.
        storage_ = static_cast<Slot*>(
            ::operator new[](capacity_ * sizeof(Slot), std::align_val_t{alignof(Slot)})
        );

        // Build freelist: indices [0, capacity) in descending order so the
        // first alloc returns slot 0 (ascending, cache-friendly).
        freelist_.reserve(capacity_);
        for (size_t i = capacity_; i-- > 0; ) {
            freelist_.push_back(static_cast<uint32_t>(i));
        }
    }

    ~MemoryPool() {
        ::operator delete[](storage_, std::align_val_t{alignof(Slot)});
    }

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // Construct a T in a free slot. Throws std::bad_alloc if pool is full.
    template<typename... Args>
    T* alloc(Args&&... args) {
        if (freelist_.empty()) throw std::bad_alloc{};
        uint32_t idx = freelist_.back();
        freelist_.pop_back();
        ++allocated_;
        return new (&storage_[idx]) T(std::forward<Args>(args)...);
    }

    // Destroy a T and return its slot to the pool.
    // Behaviour is undefined if p was not allocated from this pool,
    // or if p is already freed.
    void dealloc(T* p) {
        if (!p) return;
        p->~T();
        // Compute slot index from pointer arithmetic.
        auto* slot = reinterpret_cast<Slot*>(p);
        size_t idx = static_cast<size_t>(slot - storage_);
        // idx is in [0, capacity_) by construction — assert in debug builds.
        assert(idx < capacity_);
        freelist_.push_back(static_cast<uint32_t>(idx));
        --allocated_;
    }

    size_t capacity()  const { return capacity_; }
    size_t allocated() const { return allocated_; }
    size_t available() const { return freelist_.size(); }
    bool   full()      const { return freelist_.empty(); }

private:
    struct alignas(alignof(T)) Slot {
        std::byte data[sizeof(T)];
    };

    Slot*                storage_;
    std::vector<uint32_t> freelist_;
    size_t               capacity_;
    size_t               allocated_;
};

} // namespace astra

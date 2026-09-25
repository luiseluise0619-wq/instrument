#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace slyce {
// Bounded sequence-number queue. Producers never overwrite an unread slot.
// No allocation, locks, or payload destruction on push/pop. T must be a small
// trivially copyable record; ownership of any pointers is the caller's job.
template<class T, std::size_t Capacity> class BoundedQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "power of two required");
    static_assert(std::is_trivially_copyable<T>::value, "small POD messages only");
    struct Slot { std::atomic<std::size_t> sequence{0}; T data{}; };
    std::array<Slot, Capacity> slots;
    alignas(64) std::atomic<std::size_t> enqueuePosition{0};
    alignas(64) std::atomic<std::size_t> dequeuePosition{0};
public:
    BoundedQueue() noexcept {
        for (std::size_t i=0; i<Capacity; ++i) slots[i].sequence.store(i, std::memory_order_relaxed);
    }
    bool push(const T& value) noexcept {
        auto pos = enqueuePosition.load(std::memory_order_relaxed);
        for (;;) {
            auto& slot = slots[pos & (Capacity-1)];
            const auto seq = slot.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq - pos);
            if (diff == 0) {
                if (enqueuePosition.compare_exchange_weak(pos, pos+1, std::memory_order_relaxed)) {
                    slot.data = value;
                    slot.sequence.store(pos+1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) return false;
            else pos = enqueuePosition.load(std::memory_order_relaxed);
        }
    }
    bool pop(T& value) noexcept {
        auto pos = dequeuePosition.load(std::memory_order_relaxed);
        for (;;) {
            auto& slot = slots[pos & (Capacity-1)];
            const auto seq = slot.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq - (pos+1));
            if (diff == 0) {
                if (dequeuePosition.compare_exchange_weak(pos, pos+1, std::memory_order_relaxed)) {
                    value = slot.data;
                    slot.sequence.store(pos+Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) return false;
            else pos = dequeuePosition.load(std::memory_order_relaxed);
        }
    }
};
}

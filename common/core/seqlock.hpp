#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>


/* A single-writer, multi-reader lock-free primitive: Store() bumps a
   sequence counter around the write (odd while in progress, even once
   settled); Load() retries if it observes a torn (odd, or changed-under-us)
   sequence. T must be trivially copyable - this is a bytewise snapshot, not
   a general-purpose mutex replacement.

   Only ever call Store() from one thread; Load() is safe from any number of
   concurrent readers.

   Note: value_ is plain (non-atomic) storage read/written concurrently with
   Store(), which is technically a data race under the C++ memory model even
   though the sequence-counter protocol makes it safe on every mainstream
   compiler/architecture - the classic seqlock trade-off. */
template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>, "Seqlock<T> requires a trivially copyable T");

public:
    void Store(const T& value) {
        std::uint64_t seq = sequence_.load(std::memory_order_relaxed);
        sequence_.store(seq + 1, std::memory_order_release);  // odd: write in progress
        value_ = value;
        sequence_.store(seq + 2, std::memory_order_release);  // even: write complete
    }

    T Load() const {
        T result;
        std::uint64_t before;
        std::uint64_t after;
        do {
            before = sequence_.load(std::memory_order_acquire);
            result = value_;
            after = sequence_.load(std::memory_order_acquire);
        } while (before != after || (before & 1) != 0);
        return result;
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    T value_{};
};

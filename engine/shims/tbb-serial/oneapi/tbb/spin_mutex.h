// Serial oneTBB shim: tbb::spin_mutex. There is only one thread, so locking is a no-op.
#pragma once

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

class spin_mutex {
public:
    spin_mutex() = default;
    spin_mutex(const spin_mutex &) = delete;
    spin_mutex &operator=(const spin_mutex &) = delete;

    static constexpr bool is_rw_mutex        = false;
    static constexpr bool is_recursive_mutex = false;
    static constexpr bool is_fair_mutex      = false;

    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}

    class scoped_lock {
    public:
        scoped_lock() = default;
        explicit scoped_lock(spin_mutex &) {}
        scoped_lock(const scoped_lock &) = delete;
        scoped_lock &operator=(const scoped_lock &) = delete;

        void acquire(spin_mutex &) {}
        bool try_acquire(spin_mutex &) { return true; }
        void release() {}
    };
};

} // namespace tbb
} // namespace oneapi

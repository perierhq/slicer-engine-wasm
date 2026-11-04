// Serial oneTBB shim: tbb::task_arena and tbb::this_task_arena.
// max_concurrency() is 1. Slic3r::name_tbb_thread_pool_threads_set_locale() (Thread.cpp) runs that
// many tasks and blocks until all of them run at once, so any larger value would deadlock.
#pragma once

#include <utility>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

class task_arena {
public:
    static constexpr int automatic = -1;

    explicit task_arena(int /*max_concurrency*/ = automatic, unsigned /*reserved_for_masters*/ = 1) {}

    void initialize() {}
    void initialize(int /*max_concurrency*/, unsigned /*reserved_for_masters*/ = 1) {}
    void terminate() {}
    bool is_active() const { return true; }
    int  max_concurrency() const { return 1; }

    template<typename F> auto execute(F &&f) -> decltype(f()) { return std::forward<F>(f)(); }
    template<typename F> void enqueue(F &&f) { std::forward<F>(f)(); }
};

namespace this_task_arena {
inline int max_concurrency() { return 1; }
inline int current_thread_index() { return 0; }
template<typename F> auto isolate(F &&f) -> decltype(f()) { return std::forward<F>(f)(); }
} // namespace this_task_arena

} // namespace tbb
} // namespace oneapi

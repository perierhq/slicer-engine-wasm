// Serial oneTBB shim: tbb::parallel_for. The body sees the whole range in one call.
#pragma once

#include "blocked_range.h"
#include "partitioner.h"
// oneTBB's parallel_for.h makes this_task_arena visible too; Orca relies on that (TreeModelVolumes.cpp).
#include "task_arena.h"

namespace oneapi {
namespace tbb {

// Range form, with or without a partitioner. Partitioners are taken by const reference on purpose:
// with a forwarding reference, parallel_for(0, n, f) would resolve to the range form (Range = Body =
// int) instead of the more specialised index form below.
template<typename Range, typename Body>
void parallel_for(const Range &range, const Body &body) { body(range); }

template<typename Range, typename Body, typename Partitioner>
void parallel_for(const Range &range, const Body &body, const Partitioner &) { body(range); }

// Index forms: parallel_for(first, last[, step], f[, partitioner]).
template<typename Index, typename Function>
void parallel_for(Index first, Index last, const Function &f)
{
    for (Index i = first; i < last; ++i)
        f(i);
}

template<typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function &f)
{
    for (Index i = first; i < last; i += step)
        f(i);
}

template<typename Index, typename Function, typename Partitioner>
auto parallel_for(Index first, Index last, const Function &f, const Partitioner &)
    -> decltype(f(first), void())
{
    parallel_for(first, last, f);
}

} // namespace tbb
} // namespace oneapi

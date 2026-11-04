// Serial oneTBB shim: tbb::parallel_reduce / parallel_deterministic_reduce.
// Imperative form: body(range) accumulates into `body`. Functional form: real_body(range, identity);
// with a single range there is nothing to join, so `reduction` is never called.
#pragma once

#include "blocked_range.h"
#include "partitioner.h"

namespace oneapi {
namespace tbb {

template<typename Range, typename Body>
void parallel_reduce(const Range &range, Body &body) { body(range); }

template<typename Range, typename Body, typename Partitioner>
void parallel_reduce(const Range &range, Body &body, const Partitioner &) { body(range); }

template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &real_body, const Reduction &)
{
    return real_body(range, identity);
}

template<typename Range, typename Value, typename RealBody, typename Reduction, typename Partitioner>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &real_body, const Reduction &,
                      const Partitioner &)
{
    return real_body(range, identity);
}

template<typename Range, typename Body>
void parallel_deterministic_reduce(const Range &range, Body &body) { body(range); }

template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_deterministic_reduce(const Range &range, const Value &identity, const RealBody &real_body,
                                    const Reduction &)
{
    return real_body(range, identity);
}

} // namespace tbb
} // namespace oneapi

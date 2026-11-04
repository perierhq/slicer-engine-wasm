// Serial oneTBB shim: tbb::parallel_for_each (iterator and container forms), in sequence order.
#pragma once

#include <iterator>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

template<typename Iterator, typename Body>
void parallel_for_each(Iterator first, Iterator last, const Body &body)
{
    for (; first != last; ++first)
        body(*first);
}

template<typename Container, typename Body>
void parallel_for_each(Container &c, const Body &body)
{
    parallel_for_each(std::begin(c), std::end(c), body);
}

template<typename Container, typename Body>
void parallel_for_each(const Container &c, const Body &body)
{
    parallel_for_each(std::begin(c), std::end(c), body);
}

} // namespace tbb
} // namespace oneapi

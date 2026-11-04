// Serial oneTBB shim: tbb::scalable_allocator on plain operator new/delete.
// Deliberately a distinct type rather than an alias of std::allocator: Slic3r::Points is
// std::vector<Point, tbb::scalable_allocator<Point>> (Point.hpp) and must stay a different type from
// std::vector<Point>, exactly as with real TBB, or overloads on the two would collide.
#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

template<typename T>
class scalable_allocator {
public:
    using value_type                             = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal                        = std::true_type;

    scalable_allocator() noexcept = default;
    template<typename U> scalable_allocator(const scalable_allocator<U> &) noexcept {}

    T *allocate(std::size_t n)
    {
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            return static_cast<T *>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
        else
            return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    void deallocate(T *p, std::size_t) noexcept
    {
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            ::operator delete(p, std::align_val_t(alignof(T)));
        else
            ::operator delete(p);
    }

    template<typename U> struct rebind { using other = scalable_allocator<U>; };
};

template<typename T, typename U>
bool operator==(const scalable_allocator<T> &, const scalable_allocator<U> &) noexcept { return true; }
template<typename T, typename U>
bool operator!=(const scalable_allocator<T> &, const scalable_allocator<U> &) noexcept { return false; }

} // namespace tbb
} // namespace oneapi

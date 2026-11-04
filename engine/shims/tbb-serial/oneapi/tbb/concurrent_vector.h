// Serial oneTBB shim: tbb::concurrent_vector.
// Built on std::deque because, like concurrent_vector and unlike std::vector, it never moves existing
// elements when it grows, so references taken before a push_back stay valid. The growth functions
// return iterators, as in oneTBB.
#pragma once

#include <algorithm>
#include <deque>
#include <iterator>
#include <memory>
#include <utility>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

template<typename T, typename Allocator = std::allocator<T>>
class concurrent_vector : public std::deque<T, Allocator> {
    using base = std::deque<T, Allocator>;

public:
    using typename base::iterator;
    using typename base::size_type;

    using base::base;
    concurrent_vector() = default;

    iterator push_back(const T &item)
    {
        base::push_back(item);
        return std::prev(this->end());
    }
    iterator push_back(T &&item)
    {
        base::push_back(std::move(item));
        return std::prev(this->end());
    }
    template<typename... Args> iterator emplace_back(Args &&...args)
    {
        base::emplace_back(std::forward<Args>(args)...);
        return std::prev(this->end());
    }
    iterator grow_by(size_type delta)
    {
        const size_type old_size = this->size();
        this->resize(old_size + delta);
        return this->begin() + old_size;
    }
    iterator grow_by(size_type delta, const T &value)
    {
        const size_type old_size = this->size();
        this->resize(old_size + delta, value);
        return this->begin() + old_size;
    }
    iterator grow_to_at_least(size_type n)
    {
        const size_type old_size = this->size();
        if (old_size < n)
            this->resize(n);
        return this->begin() + std::min(old_size, n);
    }
};

} // namespace tbb
} // namespace oneapi

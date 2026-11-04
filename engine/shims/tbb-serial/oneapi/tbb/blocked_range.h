// Serial oneTBB shim: tbb::blocked_range and the splitting tags.
// The serial algorithms never split a range, but the splitting constructors keep TBB's semantics
// (the argument keeps the first half, the new range takes the second) so code written against the
// Range concept behaves the same.
#pragma once

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

class split {};

class proportional_split {
public:
    proportional_split(std::size_t left, std::size_t right) : m_left(left), m_right(right) {}
    std::size_t left() const { return m_left; }
    std::size_t right() const { return m_right; }
private:
    std::size_t m_left, m_right;
};

template<typename Value>
class blocked_range {
public:
    using const_iterator = Value;
    using size_type      = std::size_t;

    blocked_range(Value begin_, Value end_, size_type grainsize_ = 1)
        : my_end(end_), my_begin(begin_), my_grainsize(grainsize_) {}

    // Splitting constructors: r keeps [begin, middle), *this gets [middle, end).
    blocked_range(blocked_range &r, split)
        : my_end(r.my_end), my_begin(do_split(r, split())), my_grainsize(r.my_grainsize) {}
    blocked_range(blocked_range &r, proportional_split &proportion)
        : my_end(r.my_end), my_begin(do_split(r, proportion)), my_grainsize(r.my_grainsize) {}

    const_iterator begin() const { return my_begin; }
    const_iterator end() const { return my_end; }
    size_type      size() const { return size_type(my_end - my_begin); }
    size_type      grainsize() const { return my_grainsize; }
    bool           empty() const { return !(my_begin < my_end); }
    bool           is_divisible() const { return my_grainsize < size(); }

private:
    static Value do_split(blocked_range &r, split)
    {
        Value middle = r.my_begin + (r.my_end - r.my_begin) / 2u;
        r.my_end     = middle;
        return middle;
    }
    static Value do_split(blocked_range &r, proportional_split &proportion)
    {
        auto  right  = std::size_t(float(r.size()) * float(proportion.right()) /
                                   float(proportion.left() + proportion.right()) + 0.5f);
        Value middle = r.my_end - right;
        r.my_end     = middle;
        return middle;
    }

    // Declaration order as in oneTBB (end before begin), required by the splitting constructors.
    Value     my_end;
    Value     my_begin;
    size_type my_grainsize;
};

} // namespace tbb
} // namespace oneapi

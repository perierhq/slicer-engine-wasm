// Serial oneTBB shim: tbb::blocked_range2d (a pair of blocked_ranges; never split by the shim).
#pragma once

#include "blocked_range.h"

namespace oneapi {
namespace tbb {

template<typename RowValue, typename ColValue = RowValue>
class blocked_range2d {
public:
    using row_range_type = blocked_range<RowValue>;
    using col_range_type = blocked_range<ColValue>;

    blocked_range2d(RowValue row_begin, RowValue row_end, typename row_range_type::size_type row_grainsize,
                    ColValue col_begin, ColValue col_end, typename col_range_type::size_type col_grainsize)
        : my_rows(row_begin, row_end, row_grainsize), my_cols(col_begin, col_end, col_grainsize) {}
    blocked_range2d(RowValue row_begin, RowValue row_end, ColValue col_begin, ColValue col_end)
        : my_rows(row_begin, row_end), my_cols(col_begin, col_end) {}

    bool empty() const { return my_rows.empty() || my_cols.empty(); }
    bool is_divisible() const { return my_rows.is_divisible() || my_cols.is_divisible(); }

    const row_range_type &rows() const { return my_rows; }
    const col_range_type &cols() const { return my_cols; }

private:
    row_range_type my_rows;
    col_range_type my_cols;
};

} // namespace tbb
} // namespace oneapi

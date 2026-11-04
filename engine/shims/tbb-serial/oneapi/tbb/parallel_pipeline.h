// Serial oneTBB shim: tbb::parallel_pipeline, tbb::make_filter, tbb::filter, tbb::flow_control.
//
// Each token runs through every stage before the input stage is asked for the next one. With only
// serial_in_order stages (all Orca uses, GCode.cpp) that is exactly the order real TBB produces, and
// it is a valid TBB schedule for the other modes too. The stages are type-checked like oneTBB's:
// filter<T, U> & filter<U, V> -> filter<T, V>, and parallel_pipeline() takes a filter<void, void>.
// Items are moved from stage to stage, so they only need to be movable (oneTBB moves them too).
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

enum class filter_mode : unsigned int {
    parallel            = 1,
    serial_in_order     = 2,
    serial_out_of_order = 3,
};

namespace serial_detail { struct flow_access; }

class flow_control {
public:
    void stop() { m_is_pipeline_stopped = true; }

private:
    friend struct serial_detail::flow_access;
    bool m_is_pipeline_stopped = false;
};

namespace serial_detail {
struct flow_access {
    static bool stopped(const flow_control &fc) { return fc.m_is_pipeline_stopped; }
};
// `void` cannot be stored or passed, so it travels between stages as an empty tag.
struct unit {};
template<typename T> using slot = std::conditional_t<std::is_void_v<T>, unit, T>;
} // namespace serial_detail

template<typename InputType, typename OutputType>
class filter {
public:
    using input_slot  = serial_detail::slot<InputType>;
    using output_slot = serial_detail::slot<OutputType>;
    // Returns std::nullopt when the input stage called flow_control::stop(): nothing flows further.
    using stage_fn = std::function<std::optional<output_slot>(flow_control &, input_slot &&)>;

    filter() = default;
    explicit filter(stage_fn fn) : m_fn(std::move(fn)) {}

    std::optional<output_slot> operator()(flow_control &fc, input_slot &&in) const { return m_fn(fc, std::move(in)); }
    explicit operator bool() const { return bool(m_fn); }
    void clear() { m_fn = nullptr; }

private:
    stage_fn m_fn;
};

template<typename T, typename V, typename U>
filter<T, U> operator&(const filter<T, V> &left, const filter<V, U> &right)
{
    return filter<T, U>([left, right](flow_control &fc, serial_detail::slot<T> &&in) -> std::optional<serial_detail::slot<U>> {
        std::optional<serial_detail::slot<V>> mid = left(fc, std::move(in));
        if (!mid)
            return std::nullopt;
        return right(fc, std::move(*mid));
    });
}

template<typename InputType, typename OutputType, typename Body>
filter<InputType, OutputType> make_filter(filter_mode, const Body &body)
{
    using in_t  = serial_detail::slot<InputType>;
    using out_t = serial_detail::slot<OutputType>;
    return filter<InputType, OutputType>([body](flow_control &fc, in_t &&in) -> std::optional<out_t> {
        if constexpr (std::is_void_v<InputType>) {
            // Input stage: body(flow_control&). Its return value is discarded once it calls stop().
            if constexpr (std::is_void_v<OutputType>) {
                body(fc);
                if (serial_detail::flow_access::stopped(fc))
                    return std::nullopt;
                return out_t{};
            } else {
                out_t out = body(fc);
                if (serial_detail::flow_access::stopped(fc))
                    return std::nullopt;
                return std::optional<out_t>(std::move(out));
            }
        } else if constexpr (std::is_void_v<OutputType>) {
            body(std::move(in));
            return out_t{};
        } else {
            return std::optional<out_t>(body(std::move(in)));
        }
    });
}

inline void parallel_pipeline(std::size_t /*max_number_of_live_tokens*/, const filter<void, void> &filter_chain)
{
    flow_control fc;
    while (!serial_detail::flow_access::stopped(fc))
        filter_chain(fc, serial_detail::unit{});
}

} // namespace tbb
} // namespace oneapi

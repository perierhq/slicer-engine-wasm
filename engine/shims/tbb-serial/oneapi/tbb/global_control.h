// Serial oneTBB shim: tbb::global_control. Nothing to control with one thread; active_value() reports
// what the serial shim actually provides.
#pragma once

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

class global_control {
public:
    enum parameter {
        max_allowed_parallelism,
        thread_stack_size,
        terminate_on_exception,
        scheduler_handle,
        parameter_max
    };

    global_control(parameter, std::size_t) {}

    static std::size_t active_value(parameter p)
    {
        switch (p) {
        case max_allowed_parallelism: return 1;
        default: return 0;
        }
    }
};

} // namespace tbb
} // namespace oneapi

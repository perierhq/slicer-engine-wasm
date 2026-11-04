// Serial oneTBB shim: partitioner tags. The serial algorithms accept and ignore them.
#pragma once

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

class simple_partitioner {};
class auto_partitioner {};
class static_partitioner {};
class affinity_partitioner {};

} // namespace tbb
} // namespace oneapi

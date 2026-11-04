// Serial oneTBB shim: umbrella header (everything the shim provides).
#pragma once

#include "tbb/blocked_range.h"
#include "tbb/blocked_range2d.h"
#include "tbb/concurrent_unordered_map.h"
#include "tbb/concurrent_unordered_set.h"
#include "tbb/concurrent_vector.h"
#include "tbb/global_control.h"
#include "tbb/parallel_for.h"
#include "tbb/parallel_for_each.h"
#include "tbb/parallel_pipeline.h"
#include "tbb/parallel_reduce.h"
#include "tbb/partitioner.h"
#include "tbb/scalable_allocator.h"
#include "tbb/spin_mutex.h"
#include "tbb/task_arena.h"
#include "tbb/task_group.h"
#include "tbb/version.h"

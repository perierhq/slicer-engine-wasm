// Serial oneTBB shim: version macros.
// Orca selects the oneTBB API (tbb::filter_mode, tbb::global_control) with TBB_VERSION_MAJOR >= 2021
// (GCode.cpp, utils.cpp). Reporting anything older would make it include tbb/pipeline.h and
// tbb/task_scheduler_init.h, which the shim does not provide.
#pragma once

#include "detail/_serial.h"

#define TBB_VERSION_MAJOR 2021
#define TBB_VERSION_MINOR 0
#define TBB_VERSION_PATCH 0
#define TBB_VERSION_STRING "2021.0.0 (serial shim)"
#define TBB_INTERFACE_VERSION 12000
#define TBB_INTERFACE_VERSION_MAJOR 12
#define TBB_INTERFACE_VERSION_MINOR 0

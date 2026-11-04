// Serial oneTBB shim for the single-threaded engine (see ../../../README.md).
// Common prologue: namespace layout identical to oneTBB, where everything lives in oneapi::tbb and
// `tbb` is a namespace alias of it, so both spellings name the same entities.
#pragma once

#include <cstddef>

#define ORCAWASM_TBB_SERIAL_SHIM 1

namespace oneapi { namespace tbb {} }
namespace tbb = oneapi::tbb;

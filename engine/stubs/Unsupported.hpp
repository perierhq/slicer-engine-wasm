// Shared helper for the browser-engine stubs in engine/stubs/.
//
// The stubs replace libslic3r sources that need desktop-only libraries (OCCT, draco, assimp, OpenVDB)
// and define only the symbols that kept libslic3r code references (Model.cpp's file loaders, the SLA
// drain-hole helpers). A feature that cannot work in the browser throws a Slic3r::RuntimeError with a
// clear message; the engine reports it like any other Orca error.
#pragma once

#include <string>

#include "libslic3r/Exception.hpp"

namespace Slic3r {
namespace browser_engine {

[[noreturn]] inline void throw_unsupported(const std::string &feature)
{
    throw Slic3r::RuntimeError(feature + " is not supported in the browser engine.");
}

} // namespace browser_engine
} // namespace Slic3r

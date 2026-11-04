// Replaces src/libslic3r/Format/svg.cpp (OCCT) in the browser engine.
// Model::read_from_file() calls load_svg() for .svg files; building solids from SVG needs OCCT.
#include <string>

#include "libslic3r/Format/svg.hpp"

#include "../Unsupported.hpp"

namespace Slic3r {

bool load_svg(const char * /*path*/, Model * /*model*/, std::string & /*message*/)
{
    browser_engine::throw_unsupported("SVG import");
}

} // namespace Slic3r

// Replaces src/libslic3r/Platform.cpp in the browser engine.
//
// Orca's Platform.cpp has no branch for Emscripten and ends in `static_assert(false, "Unknown platform
// detected")`, which clang rejects outright. This file gives the answers Orca's own fall-through branches
// give for a platform it does not know: Platform::Unknown / PlatformFlavor::Unknown, "unknown" OS type and
// architecture. The only caller in libslic3r is copy_file_linux() (utils.cpp), which Emscripten does not
// compile.
#include "libslic3r/Platform.hpp"

#include <cassert>

#include <boost/log/trivial.hpp>

namespace Slic3r {

static auto s_platform        = Platform::Uninitialized;
static auto s_platform_flavor = PlatformFlavor::Uninitialized;

void detect_platform()
{
    BOOST_LOG_TRIVIAL(info) << "Platform: Emscripten (browser engine)";
    s_platform        = Platform::Unknown;
    s_platform_flavor = PlatformFlavor::Unknown;
}

Platform platform() { return s_platform; }

PlatformFlavor platform_flavor() { return s_platform_flavor; }

std::string platform_os_type() { return "unknown"; }

std::string platform_architecture() { return "unknown"; }

std::string platform_to_string(Platform platform)
{
    switch (platform) {
    case Platform::Uninitialized: return "Unitialized";
    case Platform::Unknown: return "Unknown";
    case Platform::Windows: return "Windows";
    case Platform::OSX: return "OSX";
    case Platform::Linux: return "Linux";
    case Platform::BSDUnix: return "BSDUnix";
    }
    assert(false);
    return "";
}

std::string platform_flavor_to_string(PlatformFlavor pf)
{
    switch (pf) {
    case PlatformFlavor::Uninitialized: return "Unitialized";
    case PlatformFlavor::Unknown: return "Unknown";
    case PlatformFlavor::Generic: return "Generic";
    case PlatformFlavor::GenericLinux: return "GenericLinux";
    case PlatformFlavor::LinuxOnChromium: return "LinuxOnChromium";
    case PlatformFlavor::WSL: return "WSL";
    case PlatformFlavor::WSL2: return "WSL2";
    case PlatformFlavor::OpenBSD: return "OpenBSD";
    case PlatformFlavor::GenericOSX: return "GenericOSX";
    case PlatformFlavor::OSXOnX86: return "OSXOnX86";
    case PlatformFlavor::OSXOnArm: return "OSXOnArm";
    }
    assert(false);
    return "";
}

} // namespace Slic3r

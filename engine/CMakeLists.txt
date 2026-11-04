# Bambu Studio's libslic3r built out of tree with Emscripten, plus a small CLI-like entry point
# (engine/bridge), as bambu-engine-<variant>.mjs + .wasm.
# Adapted from Muon3D Slicer Engine (https://github.com/Muon-3D/muon3d-slicer-engine, AGPL-3.0-only),
# engine/CMakeLists.txt at ba080b202c.
#
# Configure (Git Bash, emsdk environment; see scripts/build.sh, which does all of this):
#   emcmake cmake -C $ORCA_WASM_ROOT/prefix-<v>/share/orcawasm/initial-cache.cmake -G Ninja \
#       -S engine -B $ORCA_WASM_ROOT/build-<v> -DBAMBU_SRC=<repository>/orca -DENGINE_VARIANT=<st|mt|mt64>
#
# Nothing under BAMBU_SRC is modified: its source list is read from Orca's own CMakeLists, desktop-only
# sources are left out or replaced by engine/stubs, missing headers come from engine/shims, and the few
# wasm32 defects in Orca's code are fixed in patched copies generated in the build tree. PATCHES.md
# lists all of it.
cmake_minimum_required(VERSION 3.24)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)   # Bambu's bundled libraries declare cmake_minimum_required(2.x)
project(bambu_engine LANGUAGES C CXX)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "The engine is built with Emscripten: configure it through emcmake (see the header "
                        "of this file or scripts/build.sh).")
endif ()

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
list(PREPEND CMAKE_MODULE_PATH "${ENGINE_DIR}/cmake")

# --- Options ---------------------------------------------------------------------------------------------------
set(BAMBU_SRC "" CACHE PATH "Bambu Studio source tree (scripts/get-bambu.sh)")
set(ENGINE_VARIANT "st" CACHE STRING "Engine variant: st (single-threaded), mt (pthreads + oneTBB) or mt64 (mt with Memory64)")
set_property(CACHE ENGINE_VARIANT PROPERTY STRINGS st mt mt64)
option(ENGINE_PCH "Build libslic3r with Orca's pchheader.hpp precompiled and force-included, as Orca does (OFF is untested)" ON)
set(ENGINE_EXTRA_LINK_OPTIONS "" CACHE STRING "Additional emcc link options for the engine module (;-list)")

if (NOT EXISTS "${BAMBU_SRC}/src/libslic3r/CMakeLists.txt")
    message(FATAL_ERROR "BAMBU_SRC='${BAMBU_SRC}' is not a Bambu Studio source tree. Pass -DBAMBU_SRC=<path>.")
endif ()
if (NOT ENGINE_VARIANT MATCHES "^(st|mt|mt64)$")
    message(FATAL_ERROR "ENGINE_VARIANT must be st, mt or mt64, got '${ENGINE_VARIANT}'.")
endif ()
# The threaded variants: mt (wasm32) and mt64 (the same on wasm64, Memory64: a heap above 4 GB).
set(ENGINE_THREADED OFF)
if (ENGINE_VARIANT MATCHES "^mt")
    set(ENGINE_THREADED ON)
endif ()
set(ENGINE_WASM64 OFF)
if (ENGINE_VARIANT STREQUAL "mt64")
    set(ENGINE_WASM64 ON)
endif ()
list(APPEND CMAKE_MODULE_PATH "${BAMBU_SRC}/cmake/modules")   # Bambu's FindNLopt, Findlibnoise, ...

# --- Toolchain contract with the dependency prefix ------------------------------------------------------------
# Every translation unit must be compiled with the flags the prefix was built with (wasm exceptions,
# wasm setjmp/longjmp, -pthread for mt, BOOST_HAS_PTHREADS for st), which the prefix's initial cache puts
# into CMAKE_C_FLAGS / CMAKE_CXX_FLAGS. Refuse to configure without them rather than fail at link time.
if (NOT DEFINED ORCAWASM_VARIANT)
    message(FATAL_ERROR "Configure with the dependency prefix's initial cache: "
                        "-C <prefix-${ENGINE_VARIANT}>/share/orcawasm/initial-cache.cmake")
endif ()
if (NOT ORCAWASM_VARIANT STREQUAL ENGINE_VARIANT)
    message(FATAL_ERROR "ENGINE_VARIANT=${ENGINE_VARIANT} but the initial cache is for the "
                        "'${ORCAWASM_VARIANT}' dependency prefix.")
endif ()
foreach (flag -fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
    string(FIND " ${CMAKE_CXX_FLAGS} " " ${flag} " pos)
    if (pos EQUAL -1)
        message(FATAL_ERROR "CMAKE_CXX_FLAGS lacks ${flag}; configure with the prefix's initial cache.")
    endif ()
endforeach ()
string(FIND " ${CMAKE_CXX_FLAGS} " " -pthread " has_pthread)
if (ENGINE_THREADED AND has_pthread EQUAL -1)
    message(FATAL_ERROR "The ${ENGINE_VARIANT} variant needs -pthread in CMAKE_CXX_FLAGS (from its initial cache).")
elseif (ENGINE_VARIANT STREQUAL "st" AND NOT has_pthread EQUAL -1)
    message(FATAL_ERROR "The st variant must not be compiled with -pthread.")
endif ()
string(FIND " ${CMAKE_CXX_FLAGS} " " -m64 " has_m64)
if (ENGINE_WASM64 AND (has_m64 EQUAL -1 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8))
    message(FATAL_ERROR "The mt64 variant needs -m64 in CMAKE_C_FLAGS and CMAKE_CXX_FLAGS (from the mt64 initial cache).")
elseif (NOT ENGINE_WASM64 AND NOT has_m64 EQUAL -1)
    message(FATAL_ERROR "The ${ENGINE_VARIANT} variant must not be compiled with -m64.")
endif ()
if (ENGINE_VARIANT STREQUAL "st" AND NOT CMAKE_CXX_FLAGS MATCHES "-DBOOST_HAS_PTHREADS")
    # Without it <boost/thread.hpp> refuses to compile and Boost.Log's ABI would not match the prefix.
    message(FATAL_ERROR "The st variant needs -DBOOST_HAS_PTHREADS in CMAKE_CXX_FLAGS (from the st initial cache).")
endif ()

# --- Compile settings shared by everything (Orca's top-level CMakeLists, non-GUI Release build) --------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

add_compile_options(-fsigned-char)
# BBL_RELEASE_TO_PUBLIC / BBL_INTERNAL_TESTING come from libslic3r_version.h (1 / 0, as BuildMac.sh
# configures the released app).
add_compile_definitions(
    "$<$<CONFIG:Release>:WXINSPECTOR_DISABLE>"
    "$<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>"
    # Orca defines these globally, so libslic3r sees them too.
    wxUSE_UNICODE _UNICODE UNICODE WXINTL_NO_GETTEXT_MACRO wxNO_UNSAFE_WXSTRING_CONV)
# Not defined, as in a build without GUI and CAD: SLIC3R_GUI, SLIC3R_CAD (only AppConfig's defaults
# look at it), SLIC3R_PROFILE. SLIC3R_DESKTOP_INTEGRATION is defined by Orca's desktop builds but
# nothing in libslic3r reads it.

# wasm-specific defines, normally part of CMAKE_CXX_FLAGS from the initial cache; added here if not.
#  CGAL_ALWAYS_ROUND_TO_NEAREST: wasm cannot switch FPU rounding modes (musl's fesetround is a no-op), so
#    CGAL's interval arithmetic would be silently unsound; this makes CGAL round with nextafter instead.
#  CGAL_DISABLE_GMP: exact arithmetic from Boost.Multiprecision, no GMP/MPFR in the browser.
if (NOT CMAKE_CXX_FLAGS MATCHES "-DCGAL_ALWAYS_ROUND_TO_NEAREST")
    add_compile_definitions(CGAL_ALWAYS_ROUND_TO_NEAREST)
endif ()
if (CGAL_DISABLE_GMP AND NOT CMAKE_CXX_FLAGS MATCHES "-DCGAL_DISABLE_GMP")
    add_compile_definitions(CGAL_DISABLE_GMP)
endif ()

# Warnings: Orca's -Wall with its list of disabled categories. Orca also passes -Werror; the engine does
# not, because a newer clang targeting wasm32 finds new things to warn about in code we must not edit.
add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
    -Wno-reorder -Wno-sign-compare -Wno-misleading-indentation -Wno-switch -Wno-unused-function
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-local-typedefs
    -Wno-deprecated-declarations -Wno-ignored-attributes)

# Bambu's global include directories: src/ (for <libslic3r/...>, bundled headers), src/eigen (Bambu's
# bundled Eigen, used on macOS where no Eigen package exists), src/libigl.
include_directories(SYSTEM "${BAMBU_SRC}/src")
include_directories(BEFORE SYSTEM "${BAMBU_SRC}/src/eigen")
include_directories(SYSTEM "${BAMBU_SRC}/src/libigl")

# Reproducible output: wherever the compiler records a path (__FILE__ in throw sites and logging), this
# machine's folders appear under fixed names, so the engine's bytes do not depend on where this repository,
# the Orca checkout or the build tree sit. The dependency prefix's flags (initial cache) map ORCA_WASM_ROOT
# and the Emscripten cache the same way; the longest matching prefix wins.
add_compile_options(
    "-ffile-prefix-map=${ENGINE_DIR}=/engine"
    "-ffile-prefix-map=${BAMBU_SRC}=/bambu"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/build")

# --- Targets ---------------------------------------------------------------------------------------------------
include(Dependencies)
include(Libslic3r)
include(Engine)

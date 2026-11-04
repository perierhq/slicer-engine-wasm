#!/usr/bin/env bash
# Adapted from Muon3D Slicer Engine (https://github.com/Muon-3D/muon3d-slicer-engine, AGPL-3.0-only),
# engine/deps/ at commit ba080b202c. Changes: Bambu Studio 2.8.2.61 pins (Qhull 8.0.1,
# bambulab/libnoise 1.0.0, bundled Eigen), Emscripten 6.0.13, macOS bash 3.2 compatibility.
# =====================================================================================================
# build-deps.sh: Emscripten dependency prefixes for the OrcaSlicer libslic3r wasm engine.
# Emscripten 6.0.10 (clang 24). Written for Git Bash on Windows 11 (native, no WSL); the paths it hands to
# tools are portable (scripts/toolchain.sh), so it also runs in a Linux shell with the Linux emsdk.
#
#   VARIANT=st bash engine/deps/build-deps.sh              # every step, single-threaded prefix
#   VARIANT=mt bash engine/deps/build-deps.sh              # every step, pthreads prefix
#   VARIANT=mt64 bash engine/deps/build-deps.sh            # every step, pthreads + Memory64 (wasm64) prefix
#   VARIANT=mt bash engine/deps/build-deps.sh boost tbb    # selected steps only
#   WITH_GMP=1 VARIANT=st bash ... gmp mpfr                # optional GMP/MPFR fallback (see step_gmp)
#
# Run fetch-deps.sh first. ST and MT are independent and may build at the same time (JOBS=12 each is a
# good split on a 24-core machine).
#
# Environment:
#   ORCA_WASM_ROOT  default ~/OrcaWasm. Sources in $ORCA_WASM_ROOT/deps-src, build trees in
#                   $ORCA_WASM_ROOT/build-deps/$VARIANT (short on purpose: MAX_PATH), output in
#                   $ORCA_WASM_ROOT/prefix-$VARIANT. No spaces (it is part of the compile flags).
#   DEPS_BUILD      the build trees, instead of $ORCA_WASM_ROOT/build-deps/$VARIANT (keep it inside
#                   ORCA_WASM_ROOT, so -ffile-prefix-map still maps it).
#   ORCA_SRC        default: the orca/ submodule (read-only; only the GMP patch is taken from it).
#   ORCA_EMSDK      default $ORCA_WASM_ROOT/emsdk (scripts/toolchain.sh).
#   JOBS            parallel compile jobs, default nproc.
#   EM_CACHE        default: the emsdk's own cache. Must be on the same drive as the build trees
#                   (Emscripten's tools/system_libs.py computes relative paths between them).
#
# Output for the engine build:
#   prefix-$VARIANT/share/orcawasm/initial-cache.cmake   `emcmake cmake -C <this> -G Ninja ...`
#   prefix-$VARIANT/share/orcawasm/manifest.txt          versions, hashes, flags, step timings
#   prefix-$VARIANT/share/orcawasm/timings.log           one line per step that did work
# Every step is stamped with a hash of the emcc version + common flags + that step's recipe, so re-runs
# only rebuild what changed. See engine/deps/README.md for versions, patches and known toolchain traps.
# =====================================================================================================
set -euo pipefail

VARIANT=${VARIANT:-st}
[[ $VARIANT == st || $VARIANT == mt || $VARIANT == mt64 ]] || { echo "VARIANT must be st, mt or mt64" >&2; exit 2; }
# mt64 is mt on wasm64 (Memory64: 64-bit pointers, a heap above 4 GB). THREADED: mt and mt64.
THREADED=0; [[ $VARIANT == mt* ]] && THREADED=1
WASM64=0; [[ $VARIANT == *64 ]] && WASM64=1
WITH_GMP=${WITH_GMP:-0}   # 0 (default): CGAL uses Boost.Multiprecision, no GMP/MPFR. 1: also build them.
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../scripts/toolchain.sh
source "$SCRIPT_DIR/../scripts/toolchain.sh"
HERE=$(dir_path "$SCRIPT_DIR")    # X:/... form on Windows: passed to native tools
# Mixed-style paths (X:/...) everywhere, so no MSYS path conversion is involved when calling cmake/emcc.
ORCA_WASM_ROOT=$(mixed_path "${ORCA_WASM_ROOT:-$ORCAWASM_DEFAULT_ROOT}")
check_root "$ORCA_WASM_ROOT"
ORCA_SRC=$(mixed_path "${ORCA_SRC:-$ORCA_WASM_ROOT/BambuStudio}")
SRC=$ORCA_WASM_ROOT/deps-src
PREFIX=$ORCA_WASM_ROOT/prefix-$VARIANT
BLD=$(mixed_path "${DEPS_BUILD:-$ORCA_WASM_ROOT/build-deps/$VARIANT}")

# ---- 0. Toolchain environment ---------------------------------------------------------------------
# The emsdk's own tools first on PATH (and, on Windows, the Program Files CMake 3.31 before Strawberry
# Perl's 3.29): scripts/toolchain.sh.
setup_emsdk
check_emcc "${EMCC_PIN:-6.0.13}"
EMCC_VERSION=$(emcc --version | head -1)

# ---- 1. Flags: identical for every library AND for the engine ---------------------------------------
# * C++: native wasm exceptions (-fwasm-exceptions); C: wasm setjmp/longjmp. Both pin the *legacy* wasm EH
#   encoding (Emscripten 6.0.10's default; Safari 15.2+, Chrome 95+, Firefox 100+). Moving to exnref
#   (-sWASM_LEGACY_EXCEPTIONS=0) would drop older iPadOS: decide once, then rebuild everything.
# * SUPPORT_LONGJMP resolves to 'emscripten' (JS) for a TU compiled WITHOUT -fwasm-exceptions, and such
#   objects do not link with wasm-EH ones, so every C TU says -sSUPPORT_LONGJMP=wasm explicitly (qhull,
#   libpng and libjpeg-turbo use setjmp/longjmp).
# * MT: -pthread on every compile and link (implies atomics + bulk memory; wasm-ld rejects a non-pthread
#   object that uses TLS or atomics in a shared-memory link).
# * ST: -DBOOST_HAS_PTHREADS. Without -pthread musl sets _POSIX_THREADS=-1, Boost.Config then drops
#   BOOST_HAS_THREADS and <boost/thread.hpp> hard-errors ("no boost threads are available"). With it,
#   Boost.Thread/Log compile against Emscripten's single-threaded pthread stubs and Boost.Log keeps the
#   same v2s_mt_posix ABI in ST and MT. BOOST_LOG_NO_THREADS is NOT an option: Orca 2.5's utils.cpp
#   includes boost/log/sinks/async_frontend.hpp, which #errors under it.
# * No -flto (deps stay plain wasm objects; the engine link may still use LTO) and no -msimd128 (keeps
#   Eigen/CGAL arithmetic scalar, like the reference x86-64 build without AVX).
# * Reproducible output: -ffile-prefix-map writes this machine's folders as fixed names wherever the
#   compiler records a path (__FILE__ in Boost.Log's and Boost.Multiprecision's throw sites ends up in the
#   engine's wasm): ORCA_WASM_ROOT (sources, prefix, build trees) as /orcawasm and the Emscripten cache
#   (sysroot) as /emcache. The longest matching prefix wins, whatever the order. The flags reach the engine
#   through the initial cache; engine/CMakeLists.txt maps the engine's own folders.
SJLJ="-sSUPPORT_LONGJMP=wasm -sWASM_LEGACY_EXCEPTIONS=1"
EH="-fwasm-exceptions $SJLJ"
# * mt64: -m64 on every compile and link (wasm64-unknown-emscripten: 64-bit size_t and pointers, Memory64).
#   Emscripten.cmake sees it in CMAKE_C_FLAGS and sets CMAKE_SIZEOF_VOID_P=8 and the wasm64-emscripten
#   library architecture; -sMEMORY64=1 means the same, deprecated in favour of -m64 since Emscripten 6.0.1.
THR=""; [[ $THREADED == 1 ]] && THR="-pthread"
[[ $WASM64 == 1 ]] && THR="$THR -m64"
for dir in "$ORCA_WASM_ROOT" "$EM_CACHE"; do
  [[ $dir != *" "* ]] || { echo "No spaces allowed in '$dir': it is written into the compile flags." >&2; exit 2; }
done
PFX_MAP="-ffile-prefix-map=$ORCA_WASM_ROOT=/orcawasm -ffile-prefix-map=$EM_CACHE=/emcache"
CFLAGS_V="$SJLJ $THR $PFX_MAP"
CXXFLAGS_V="$EH $THR $PFX_MAP"
[[ $VARIANT == st ]] && CXXFLAGS_V="$CXXFLAGS_V -DBOOST_HAS_PTHREADS"
LDFLAGS_V="$EH $THR"
CFLAGS_V=$(echo $CFLAGS_V); CXXFLAGS_V=$(echo $CXXFLAGS_V); LDFLAGS_V=$(echo $LDFLAGS_V)   # squeeze spaces

# Defines every C++ translation unit of the ENGINE must carry on top of CXXFLAGS_V (baked into the initial
# cache's CMAKE_CXX_FLAGS so a consumer cannot forget them; they do not change how the libraries in the
# prefix compile, CGAL being header-only):
# * CGAL_ALWAYS_ROUND_TO_NEAREST: wasm has no FPU rounding modes (musl's fesetround is a no-op), so CGAL's
#   interval filters would be silently unsound; with it CGAL builds its intervals with nextafter.
# * CGAL_DISABLE_GMP: exact number types come from Boost.Multiprecision (see step_cgal).
ENGINE_DEFS="-DCGAL_ALWAYS_ROUND_TO_NEAREST"
[[ $WITH_GMP == 1 ]] || ENGINE_DEFS="$ENGINE_DEFS -DCGAL_DISABLE_GMP"

# find_package strategy (verified against this Emscripten.cmake): the toolchain APPENDS its sysroot to
# CMAKE_FIND_ROOT_PATH and sets MODE_{INCLUDE,LIBRARY,PACKAGE}=ONLY. CMAKE_PREFIX_PATH alone gets
# re-rooted into the sysroot and finds nothing; the prefix in CMAKE_FIND_ROOT_PATH is found and keeps the
# ONLY mode, so nothing from the host (Strawberry, anaconda, Program Files) can leak into a wasm build.
CM_COMMON=(
  -G Ninja                                   # REQUIRED: without -G, emcmake picks "MinGW Makefiles"
                                             # because Strawberry's mingw32-make is on PATH
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  # Typed on purpose: a project that re-declares it as a PATH cache entry (libjpeg-turbo's own
  # GNUInstallDirs) would otherwise turn an untyped relative value into <current directory>/lib.
  -DCMAKE_INSTALL_LIBDIR:PATH=lib
  -DCMAKE_FIND_ROOT_PATH="$PREFIX"
  -DCMAKE_PREFIX_PATH="$PREFIX"
  -DCMAKE_C_FLAGS="$CFLAGS_V"
  -DCMAKE_CXX_FLAGS="$CXXFLAGS_V"
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS_V"
  -DBUILD_SHARED_LIBS=OFF
  -DCMAKE_POSITION_INDEPENDENT_CODE=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5         # no-op on 3.31; lets CMake >= 4 configure the old projects
)

mkdir -p "$PREFIX/share/orcawasm" "$PREFIX/.stamps" "$BLD"
cd "$BLD"   # anything a sub-build resolves against the current directory lands in the build tree
# Stamps: a step is skipped when it already ran with the same toolchain + common flags (FLAGS_ID) and the
# same recipe, i.e. the same text of its step_* function (STEP_ID, set by the main loop below).
FLAGS_ID=$(printf '%s|' "$EMCC_VERSION" "$CFLAGS_V" "$CXXFLAGS_V" "$LDFLAGS_V" "${CM_COMMON[*]}" \
  | sha256sum | cut -c1-12)
STEP_ID=""
STEP_SKIPPED=0          # set when a step had nothing to do, so the timing log only lists real work
TIMINGS=$PREFIX/share/orcawasm/timings.log

done_already() { [[ -f "$PREFIX/.stamps/$1-$FLAGS_ID$STEP_ID" ]] && STEP_SKIPPED=1; }
mark_done()    { rm -f "$PREFIX/.stamps/$1-"*; touch "$PREFIX/.stamps/$1-$FLAGS_ID$STEP_ID"; }

# cmake_dep <stamp> <srcdir> [--target T] [extra -D...]: configure, build, install one CMake project.
cmake_dep() {
  local stamp=$1 src=$2; shift 2
  local target=()
  if [[ ${1:-} == --target ]]; then target=(--target "$2"); shift 2; fi
  if done_already "$stamp"; then echo "[skip] $stamp"; return; fi
  echo "==== [$VARIANT] $stamp"
  rm -rf "${BLD:?}/$stamp"
  emcmake cmake -S "$src" -B "$BLD/$stamp" "${CM_COMMON[@]}" "$@"
  cmake --build "$BLD/$stamp" -j "$JOBS" ${target[@]+"${target[@]}"}
  cmake --install "$BLD/$stamp"
  mark_done "$stamp"
}

# ---- 2. Emscripten ports: zlib 1.3.2, libpng 1.6.58 ------------------------------------------------
# Pinned by the emsdk (sha512 in upstream/emscripten/tools/ports/*.py). They only matter for PNG reading
# and writing (PNGReadWrite, EdgeGrid debug output); G-code thumbnails are PNG-encoded by miniz, and the
# CLI path writes none, so their versions do not affect G-code parity with Orca's pins (1.2.13 / 1.6.35).
# libpng uses setjmp: the -legacysjlj variants are the ones matching our flags ('mt-' for pthreads).
# Pre-building here keeps the engine build offline; step_handoff points FindZLIB/FindPNG at the archives.
# libjpeg is NOT the port: Orca's GCode/Thumbnails.cpp needs libjpeg-turbo's JCS_EXT_RGBA (step_jpeg).
png_port() { [[ $THREADED == 1 ]] && echo libpng-mt-legacysjlj || echo libpng-legacysjlj; }
port_lib_dir() {
  if [[ $WASM64 == 1 ]]; then echo "$EM_CACHE/sysroot/lib/wasm64-emscripten"; else echo "$EM_CACHE/sysroot/lib/wasm32-emscripten"; fi
}
step_ports() {
  # The archives live in the shared Emscripten cache (not the prefix), so check them too: an
  # `emcc --clear-cache` must not leave a stale stamp behind.
  if [[ -f "$(port_lib_dir)/libz.a" && -f "$(port_lib_dir)/$(png_port).a" ]] && done_already ports; then
    echo "[skip] ports"; return
  fi
  local arch=(); [[ $WASM64 == 1 ]] && arch=(--wasm64)
  embuilder ${arch[@]+"${arch[@]}"} build zlib "$(png_port)"
  mark_done ports
}

# ---- 3. Boost 1.84.0 (Orca pin; the CMake-enabled release tarball) ----------------------------------
# Boost's own CMake build (as Orca's deps build does), not b2. Everything except libraries that cannot or
# need not build for wasm; all headers are installed (libslic3r and CGAL use ~60 header-only Boost libs).
# * context: asio's CMake target INTERFACE-links Boost::context + Boost::coroutine and Boost.Log links asio,
#   so context cannot be excluded. Its default fcontext implementation would assemble i386 .S files
#   (Emscripten.cmake sets CMAKE_SYSTEM_PROCESSOR=x86); the ucontext implementation compiles against
#   Emscripten's <ucontext.h>. Nothing in libslic3r uses it, so the unimplemented getcontext/makecontext
#   are never linked.
# * Components Orca's CMakeLists requests (all produced): system filesystem thread log log_setup locale
#   regex chrono atomic date_time iostreams program_options nowide.
# * Locale: ICU off (as in Orca) -> boost::locale::normalize() is a no-op, as in Orca's Linux build.
#   Boost only defaults the POSIX backend on for Linux|Darwin; force it (musl has newlocale & co.) so the
#   default backend is the same as in the reference Linux build (posix, with std and iconv next to it).
# * iostreams: no compression filters; libslic3r only uses array devices and mapped_file (Format/DRC.cpp).
step_boost() {
  cmake_dep boost-1.84.0 "$SRC/boost-1.84.0" \
    -DBOOST_EXCLUDE_LIBRARIES="contract;fiber;numpy;stacktrace;wave;test;cobalt;coroutine2;python;mpi;graph_parallel;property_map_parallel" \
    -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF \
    -DBOOST_INSTALL_LAYOUT=system \
    -DBOOST_CONTEXT_IMPLEMENTATION=ucontext \
    -DBOOST_LOCALE_ENABLE_ICU=OFF -DBOOST_LOCALE_ENABLE_POSIX=ON \
    -DBOOST_IOSTREAMS_ENABLE_ZLIB=OFF -DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF \
    -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF -DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF \
    -DBOOST_LOG_WITHOUT_SYSLOG=ON -DBOOST_LOG_WITHOUT_IPC=ON
}

# ---- 4. Header-only packages (installed for their CMake package files) --------------------------------
step_eigen() {    # Eigen 5.0.1 (Orca pin). Orca's CMake requires find_package(Eigen3 5.0.1).
  cmake_dep eigen-5.0.1 "$SRC/eigen-5.0.1" \
    -DBUILD_TESTING=OFF -DEIGEN_BUILD_TESTING=OFF -DEIGEN_BUILD_BLAS=OFF -DEIGEN_BUILD_LAPACK=OFF \
    -DEIGEN_BUILD_DOC=OFF -DEIGEN_BUILD_DEMOS=OFF -DEIGEN_BUILD_PKGCONFIG=OFF -DEIGEN_BUILD_CMAKE_PACKAGE=ON
}
step_cereal() {   # cereal 1.3.0 (Orca pin) -> share/cmake/cereal/cereal-config.cmake, target `cereal`
  cmake_dep cereal-1.3.0 "$SRC/cereal-1.3.0" \
    -DJUST_INSTALL_CEREAL=ON -DSKIP_PERFORMANCE_COMPARISON=ON -DBUILD_TESTS=OFF -DBUILD_SANDBOX=OFF -DBUILD_DOC=OFF
}
step_cgal() {     # CGAL 5.6.3 (Orca pin), header-only.
  # GMP policy is the consumer's: with CGAL_DISABLE_GMP=ON set before find_package(CGAL) (the initial cache
  # does), CGAL::CGAL carries CGAL_DISABLE_GMP=1 and the exact number types come from Boost.Multiprecision
  # (Boost >= 1.80). FFF slicing only evaluates exact predicates (Voronoi checks behind Arachne), whose
  # results do not depend on the exact number type.
  cmake_dep cgal-5.6.3 "$SRC/CGAL-5.6.3"
}

# ---- 5. Small compiled libraries --------------------------------------------------------------------
step_nlopt() {    # NLopt 2.5.0 (Orca pin), C only. Only the library: 2.5.0 always adds test/ (testopt).
  cmake_dep nlopt-2.5.0 "$SRC/nlopt-2.5.0" --target nlopt \
    -DNLOPT_CXX=OFF -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF -DNLOPT_GUILE=OFF \
    -DNLOPT_SWIG=OFF -DNLOPT_LINK_PYTHON=OFF
}
step_libnoise() { # libnoise 1.0 (SoftFever fork, Orca pin) -> lib/liblibnoise_static.a + include/libnoise/
  cmake_dep libnoise-1.0.0 "$SRC/libnoise-1.0.0"
}
step_qhull() {    # Qhull 8.0.2 (Orca pin for Linux/macOS; the reference Linux CLI uses it).
  # Exports Qhull::qhullcpp + Qhull::qhullstatic_r (AnyNewerVersion, so deps_src/qhull's
  # find_package(Qhull 7.2) picks it up). Its small apps are built too; harmless.
  cmake_dep qhull-8.0.1 "$SRC/qhull-8.0.1" \
    -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DLINK_APPS_SHARED=OFF -DINCLUDE_INSTALL_DIR=include
}

# ---- 6. libjpeg-turbo 3.0.1 (Orca pin) ----------------------------------------------------------------
# Required, not the Emscripten libjpeg port: GCode/Thumbnails.cpp compresses JPEG thumbnails with
# in_color_space = JCS_EXT_RGBA, a libjpeg-turbo extension that IJG libjpeg 9f (the port) lacks.
# WITH_JPEG8 as in Orca's Linux build (deps/JPEG/JPEG.cmake); no SIMD (x86 NASM/intrinsics only), no
# TurboJPEG API (libslic3r uses the libjpeg API only). find_package(JPEG) finds it via CMake's FindJPEG.
# BUILD: the build string in its version message ("libjpeg-turbo version 3.0.1 (build ...)"), which reaches the
# engine's wasm. It defaults to the day of the configure run, which made prefixes built on different days differ;
# it is fixed to the release's date (the 3.0.1 tag, 2023-10-11) instead.
step_jpeg() {
  cmake_dep libjpeg-turbo-3.0.1 "$SRC/libjpeg-turbo-3.0.1" \
    -DBUILD=20231011 \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_JPEG8=ON -DWITH_SIMD=OFF -DREQUIRE_SIMD=OFF \
    -DWITH_TURBOJPEG=OFF -DWITH_JAVA=OFF -DWITH_FUZZ=OFF
}

# ---- 7. oneTBB 2021.12.0 (MT only) ------------------------------------------------------------------
# Orca pins 2021.5.0 (no wasm support). 2021.11.0 is the first release with Emscripten support, but its
# Clang profile adds -mrtm/-mwaitpkg whenever CMAKE_SYSTEM_PROCESSOR matches x86, which Emscripten.cmake
# sets (fixed upstream in 8259efb, first released in 2021.12.0) -> 2021.12.0 is the smallest bump that
# configures. Its API is 2021.x, as Orca's code expects.
# Patch: the Emscripten block of cmake/compilers/Clang.cmake adds -fexceptions (the JS exception model);
# drop it, -fwasm-exceptions comes from CMAKE_CXX_FLAGS. tbbmalloc keeps its own -fno-exceptions, which
# emcc accepts next to -fwasm-exceptions.
# ST builds no TBB: the engine compiles libslic3r against a serial header shim (engine/shims).
step_tbb() {
  [[ $THREADED == 1 ]] || { echo "[skip] tbb (ST uses the serial shim)"; STEP_SKIPPED=1; return; }
  done_already onetbb-2021.12.0 && { echo "[skip] onetbb-2021.12.0"; return; }
  local tsrc=$BLD/src-oneTBB-2021.12.0
  rm -rf "$tsrc"; cp -r "$SRC/oneTBB-2021.12.0" "$tsrc"          # keep deps-src pristine
  sed -i.orig 's/^\(  set(TBB_COMMON_COMPILE_FLAGS \${TBB_COMMON_COMPILE_FLAGS}\) -fexceptions)/  # orcawasm: -fexceptions removed (-fwasm-exceptions comes from CMAKE_CXX_FLAGS)/' \
    "$tsrc/cmake/compilers/Clang.cmake"
  if grep -n 'TBB_COMMON_COMPILE_FLAGS[^#]*-fexceptions' "$tsrc/cmake/compilers/Clang.cmake"; then
    echo "oneTBB Clang.cmake patch did not apply" >&2; exit 1
  fi
  cmake_dep onetbb-2021.12.0 "$tsrc" \
    -DTBB_TEST=OFF -DTBB_EXAMPLES=OFF -DTBB_STRICT=OFF -DTBB_ENABLE_IPO=OFF -DTBB4PY_BUILD=OFF \
    -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON -DTBBMALLOC_BUILD=ON -DTBBMALLOC_PROXY_BUILD=OFF
}

# ---- 8. OPTIONAL fallback: GMP 6.2.1 + MPFR 4.2.2 (Orca pins; WITH_GMP=1) -----------------------------
# Not needed by default (see step_cgal); build only if parity tests implicate CGAL constructions.
# UNTESTED: autotools on Windows needs sh (Git Bash), a make (Strawberry's gmake), a host C compiler for
# GMP's generators (Strawberry gcc) and m4, which this machine lacks (M4=m4 skips the probe; with
# --disable-assembly m4 is never run). If it fails, build the two .a files on any Linux box with the same
# emsdk and copy include/{gmp.h,mpfr.h,mpf2mpfr.h} + lib/{libgmp.a,libmpfr.a} into the prefix.
step_gmp() {
  [[ $WITH_GMP == 1 ]] || { echo "[skip] gmp (WITH_GMP=0)"; STEP_SKIPPED=1; return; }
  done_already gmp-6.2.1 && { echo "[skip] gmp"; return; }
  local b=$BLD/gmp-6.2.1; rm -rf "$b"; cp -r "$SRC/gmp-6.2.1" "$b"
  (
    cd "$b"
    patch -p1 < "$ORCA_SRC/deps/GMP/0001-GMP_GCC15.patch"
    M4=m4 CC_FOR_BUILD=/c/Strawberry/c/bin/gcc.exe \
      emconfigure sh ./configure --host=wasm32-unknown-emscripten --build=x86_64-w64-mingw32 \
        --disable-assembly --disable-shared --enable-static --disable-cxx --prefix="$PREFIX" \
        CFLAGS="-O2 $CFLAGS_V"
    emmake /c/Strawberry/c/bin/gmake.exe -j "$JOBS"
    /c/Strawberry/c/bin/gmake.exe install
  )
  mark_done gmp-6.2.1
}
step_mpfr() {
  [[ $WITH_GMP == 1 ]] || { echo "[skip] mpfr (WITH_GMP=0)"; STEP_SKIPPED=1; return; }
  done_already mpfr-4.2.2 && { echo "[skip] mpfr"; return; }
  local b=$BLD/mpfr-4.2.2; rm -rf "$b"; cp -r "$SRC/mpfr-4.2.2" "$b"
  (
    cd "$b"
    emconfigure sh ./configure --host=wasm32-unknown-emscripten --build=x86_64-w64-mingw32 \
      --disable-shared --enable-static --with-gmp="$PREFIX" --prefix="$PREFIX" CFLAGS="-O2 $CFLAGS_V"
    emmake /c/Strawberry/c/bin/gmake.exe -j "$JOBS"
    /c/Strawberry/c/bin/gmake.exe install
  )
  mark_done mpfr-4.2.2
}

# ---- 9. Hand-off file for the engine build ----------------------------------------------------------
# initial-cache.cmake carries everything a consumer must agree on: the flags, the engine defines, where to
# find packages, and FindZLIB/FindPNG hints that resolve to this variant's Emscripten port archives.
step_handoff() {
  local ic=$PREFIX/share/orcawasm/initial-cache.cmake
  local sysroot=$EM_CACHE/sysroot
  local portlib; portlib=$(port_lib_dir)
  local f
  for f in "$portlib/libz.a" "$portlib/$(png_port).a" "$sysroot/include/zlib.h" "$sysroot/include/png.h"; do
    [[ -f $f ]] || { echo "missing port file $f (run the 'ports' step)" >&2; exit 1; }
  done
  cat > "$ic" <<EOF
# Generated by engine/deps/build-deps.sh ($VARIANT, flags id $FLAGS_ID). Do not edit; re-run the script.
# Use:  emcmake cmake -C "$ic" -G Ninja -S <src> -B <build>
# The flags below are the ones every dependency in this prefix was compiled with. The engine must use
# exactly these for every C/C++ translation unit and for the final link (append, do not replace).
set(ORCAWASM_VARIANT "$VARIANT" CACHE STRING "OrcaWasm dependency variant (st|mt|mt64)")
set(ORCAWASM_PREFIX "$PREFIX" CACHE PATH "OrcaWasm dependency prefix")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
# C++ adds the engine-only defines: CGAL_ALWAYS_ROUND_TO_NEAREST (no FPU rounding modes in wasm, CGAL's
# interval filters are otherwise silently wrong) and CGAL_DISABLE_GMP (Boost.Multiprecision exact types).
set(CMAKE_C_FLAGS "$CFLAGS_V" CACHE STRING "")
set(CMAKE_CXX_FLAGS "$CXXFLAGS_V $ENGINE_DEFS" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS "$LDFLAGS_V" CACHE STRING "")

# Package search: the prefix is a find root (Emscripten.cmake appends its sysroot after it and sets the
# find modes to ONLY, so nothing is taken from the host).
set(CMAKE_FIND_ROOT_PATH "$PREFIX" CACHE STRING "")
set(CMAKE_PREFIX_PATH "$PREFIX" CACHE STRING "")
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW CACHE STRING "")   # find_package(Boost) -> BoostConfig.cmake
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "")
set(CGAL_DISABLE_GMP $([[ $WITH_GMP == 1 ]] && echo OFF || echo ON) CACHE BOOL "")
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "")
# Orca's own find modules (FindNLopt, Findlibnoise, FindTBB, Findcereal) work with this prefix; plain
# CONFIG-mode find_package works for Boost, Eigen3, cereal, CGAL, NLopt, Qhull$([[ $THREADED == 1 ]] && echo ", TBB").

# zlib + libpng: Emscripten ports (built by the 'ports' step). These hints make the stock FindZLIB and
# FindPNG return ZLIB::ZLIB / PNG::PNG pointing at this variant's archives, so Orca's find_package(ZLIB)
# and find_package(PNG) work unchanged. Equivalent alternative: compile+link with \${ORCAWASM_PORT_OPTIONS}.
# Do NOT use -sUSE_LIBJPEG: JPEG comes from libjpeg-turbo in the prefix (find_package(JPEG)).
set(ZLIB_INCLUDE_DIR "$sysroot/include" CACHE PATH "")
set(ZLIB_LIBRARY "$portlib/libz.a" CACHE FILEPATH "")
set(PNG_PNG_INCLUDE_DIR "$sysroot/include" CACHE PATH "")
set(PNG_LIBRARY "$portlib/$(png_port).a" CACHE FILEPATH "")
set(ORCAWASM_PORT_OPTIONS "-sUSE_ZLIB=1;-sUSE_LIBPNG=1" CACHE STRING "")
EOF
  {
    echo "variant=$VARIANT  emcc=$EMCC_VERSION  flags_id=$FLAGS_ID  written=$(date -u +%FT%TZ)"
    echo "CFLAGS=$CFLAGS_V"; echo "CXXFLAGS=$CXXFLAGS_V"; echo "LDFLAGS=$LDFLAGS_V"
    echo "engine-only C++ defines=$ENGINE_DEFS"
    echo "boost 1.84.0, eigen 5.0.1, cereal 1.3.0, CGAL 5.6.3 (headers), nlopt 2.5.0, libnoise 1.0, qhull 8.0.2,"
    echo "libjpeg-turbo 3.0.1 (jpeg8 API)$([[ $THREADED == 1 ]] && echo ', oneTBB 2021.12.0 (Clang.cmake -fexceptions patch)')"
    echo "ports: zlib 1.3.2 + $(png_port) 1.6.58 (emsdk 6.0.10 tools/ports), in $portlib"
    echo "--- source archives (sha256)"; cat "$HERE/SHA256SUMS"
    echo "--- step timings (UTC end time, step, seconds, parallel jobs)"; [[ -f $TIMINGS ]] && cat "$TIMINGS"
  } > "$PREFIX/share/orcawasm/manifest.txt"
  echo "wrote $ic"
}

# ---- 10. Verify + smoke test (one wasm program linked against the whole prefix, run under node) --------
step_verify() {
  local miss=0 f
  local want=(
    lib/cmake/Boost-1.84.0/BoostConfig.cmake
    lib/libboost_filesystem.a lib/libboost_thread.a lib/libboost_log.a lib/libboost_log_setup.a
    lib/libboost_locale.a lib/libboost_nowide.a lib/libboost_chrono.a lib/libboost_atomic.a
    lib/libboost_iostreams.a lib/libboost_program_options.a lib/libboost_date_time.a
    include/boost/version.hpp include/boost/multiprecision/cpp_int.hpp include/boost/polygon/voronoi.hpp
    share/eigen3/cmake/Eigen3Config.cmake include/eigen3/Eigen/Core
    share/cmake/cereal/cereal-config.cmake include/cereal/cereal.hpp
    lib/cmake/CGAL/CGALConfig.cmake include/CGAL/version.h
    lib/libnlopt.a include/nlopt.h
    lib/liblibnoise_static.a include/libnoise/noise.h
    lib/libqhullcpp.a lib/libqhullstatic_r.a lib/cmake/Qhull/QhullConfig.cmake
    lib/libjpeg.a include/jpeglib.h include/jconfig.h
    share/orcawasm/initial-cache.cmake
  )
  [[ $THREADED == 1 ]] && want+=(lib/libtbb.a lib/libtbbmalloc.a lib/cmake/TBB/TBBConfig.cmake)
  [[ $WITH_GMP == 1 ]] && want+=(lib/libgmp.a include/gmp.h lib/libmpfr.a include/mpfr.h)
  for f in "${want[@]}"; do [[ -e "$PREFIX/$f" ]] || { echo "MISSING $f"; miss=1; }; done
  [[ $miss == 0 ]] || { echo "prefix incomplete" >&2; exit 1; }
  # Same find_package calls the engine makes, plus EH, sjlj, ports and (MT) TBB threads, run under node.
  rm -rf "${BLD:?}/smoke"
  emcmake cmake -C "$PREFIX/share/orcawasm/initial-cache.cmake" -G Ninja -S "$HERE/smoke" -B "$BLD/smoke" \
    -DORCA_SRC="$ORCA_SRC"
  cmake --build "$BLD/smoke" -j "$JOBS"
  node "$BLD/smoke/smoke.js"
}

ALL=(ports boost cereal cgal nlopt libnoise qhull jpeg tbb handoff)
if [[ $# -eq 0 ]]; then STEPS=("${ALL[@]}"); else STEPS=("$@"); fi
for s in "${STEPS[@]}"; do
  declare -F "step_$s" >/dev/null || { echo "unknown step '$s' (steps: ${ALL[*]})" >&2; exit 2; }
done
for s in "${STEPS[@]}"; do
  STEP_ID=-$(declare -f "step_$s" | sha256sum | cut -c1-8)
  STEP_SKIPPED=0
  t0=$SECONDS
  "step_$s"
  if [[ $STEP_SKIPPED == 0 ]]; then
    printf '%s  %-8s %5ds  JOBS=%s\n' "$(date -u +%FT%TZ)" "$s" $((SECONDS - t0)) "$JOBS" >> "$TIMINGS"
    echo "[time] $s $((SECONDS - t0))s"
  fi
done
echo "OK: $PREFIX ($VARIANT)"

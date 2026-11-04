# shellcheck shell=bash
# =====================================================================================================
# toolchain.sh: the environment the engine scripts share. Sourced (never run) by scripts/build.sh,
# scripts/get-orca.sh, deps/build-deps.sh and deps/fetch-deps.sh.
#
# On Windows (Git Bash) every path handed to a native tool (cmake, emcc, ninja, node) is written
# mixed-style (X:/OrcaWasm/...), which needs no MSYS conversion. Elsewhere (Linux, e.g. the
# emscripten/emsdk container) paths are used as they are.
#
#   ORCAWASM_DEFAULT_ROOT  $HOME/OrcaWasm (ORCA_WASM_ROOT's default)
#   mixed_path P    X:/OrcaWasm on Windows, P unchanged elsewhere
#   unix_path P     /x/OrcaWasm on Windows (for PATH), P unchanged elsewhere
#   native_path P   X:\OrcaWasm on Windows (for Windows programs that parse their own arguments)
#   dir_path D      the absolute path of folder D, in mixed_path form
#   check_root R    stops when the workspace root R contains a space
#   setup_emsdk     exports EMSDK, EM_CONFIG, EM_CACHE, EMSDK_NODE (+ EMSDK_PYTHON on Windows) and puts
#                   Emscripten and its node first on PATH; ORCA_WASM_ROOT must be set first
#   check_emcc V    stops unless `emcc --version` is Emscripten V
#
# Environment read by setup_emsdk:
#   ORCA_EMSDK  the emsdk folder (default $ORCA_WASM_ROOT/emsdk), activated with `./emsdk activate 6.0.10`
#   EM_CACHE    default: the emsdk's own cache. Keep it on the same drive as ORCA_WASM_ROOT: Emscripten's
#               tools/system_libs.py computes relative paths between the cache and the build trees.
# =====================================================================================================

if command -v cygpath >/dev/null 2>&1; then
  ORCAWASM_WINDOWS=1
  ORCAWASM_DEFAULT_ROOT=$HOME/OrcaWasm
  mixed_path() { cygpath -m "$1"; }
  unix_path() { cygpath -u "$1"; }
  native_path() { cygpath -w "$1"; }
else
  ORCAWASM_WINDOWS=0
  ORCAWASM_DEFAULT_ROOT=$HOME/OrcaWasm
  mixed_path() { printf '%s\n' "$1"; }
  unix_path() { printf '%s\n' "$1"; }
  native_path() { printf '%s\n' "$1"; }
fi
# `pwd -W` is Git Bash's X:/... form of the current directory; other shells reject the option.
dir_path() { (cd "$1" && { pwd -W 2>/dev/null || pwd; }); }

# The workspace root goes into every compile flag (-ffile-prefix-map) and Emscripten's cache paths, so it
# must not contain a space (on Windows the default ~/OrcaWasm does when the user name has one).
check_root() {
  [[ $1 != *" "* ]] || {
    echo "ORCA_WASM_ROOT=$1 contains a space: set ORCA_WASM_ROOT to a folder without one." >&2
    exit 1
  }
}

setup_emsdk() {
  : "${ORCA_WASM_ROOT:?set ORCA_WASM_ROOT before setup_emsdk}"
  # Deterministic replacement for `source emsdk_env.sh`: only the emsdk's own tools, no other state.
  export EMSDK
  EMSDK=$(mixed_path "${ORCA_EMSDK:-$ORCA_WASM_ROOT/emsdk}")
  if [[ ! -f $EMSDK/.emscripten ]]; then
    echo "No activated emsdk in $EMSDK (set ORCA_EMSDK, or: git clone https://github.com/emscripten-core/emsdk" \
         "\"$EMSDK\" && cd \"$EMSDK\" && ./emsdk install 6.0.10 && ./emsdk activate 6.0.10)." >&2
    exit 1
  fi
  export EM_CONFIG=$EMSDK/.emscripten
  export EM_CACHE
  EM_CACHE=$(mixed_path "${EM_CACHE:-$EMSDK/upstream/emscripten/cache}")
  local node_dir
  node_dir=$(ls -d "$EMSDK"/node/*_64bit 2>/dev/null | sort -V | tail -1) || true
  [[ -n $node_dir ]] || { echo "No node in $EMSDK/node: install and activate the emsdk (see above)." >&2; exit 1; }
  if [[ $ORCAWASM_WINDOWS == 1 ]]; then
    # emsdk 6 on Windows ships node.exe/python.exe at the top of their folders, and .exe launchers
    # for emcc & co. that Git Bash runs directly. The Program Files CMake (3.31) must come before
    # Strawberry Perl's older one, when both are installed.
    local py_dir cmake_dir=""
    py_dir=$(ls -d "$EMSDK"/python/*_64bit 2>/dev/null | sort -V | tail -1) || true
    export EMSDK_NODE=$node_dir/node.exe
    [[ -n $py_dir ]] && export EMSDK_PYTHON=$py_dir/python.exe
    [[ -d "/c/Program Files/CMake/bin" ]] && cmake_dir=":/c/Program Files/CMake/bin"
    PATH="$(unix_path "$EMSDK/upstream/emscripten"):$(unix_path "$node_dir")$cmake_dir:$PATH"
  else
    # Linux/macOS emsdk: node in bin/, python from the system (python3 on PATH).
    export EMSDK_NODE=$node_dir/bin/node
    # macOS often has an older python3 first on PATH; emsdk ships its own (Emscripten needs >= 3.10).
    local py_dir; py_dir=$(ls -d "$EMSDK"/python/*_64bit 2>/dev/null | sort -V | tail -1) || true
    [[ -n $py_dir && -x $py_dir/bin/python3 ]] && export EMSDK_PYTHON=$py_dir/bin/python3
    PATH="$EMSDK/upstream/emscripten:$node_dir/bin:$PATH"
  fi
  export PATH
  command -v emcc >/dev/null || { echo "emcc not found under $EMSDK/upstream/emscripten" >&2; exit 1; }
  command -v ninja >/dev/null || {
    echo "ninja not on PATH (Windows: winget install Ninja-build.Ninja; Linux: apt install ninja-build)" >&2
    exit 1
  }
}

check_emcc() {
  local want=$1 got
  got=$(emcc --version | head -1)
  [[ $got == *" $want "* ]] || { echo "Expected Emscripten $want in $EMSDK, got: $got" >&2; exit 1; }
}

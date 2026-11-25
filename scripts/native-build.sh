#!/bin/sh
# Shared native source/build/cache contract. Invoke from the repository root.
set -eu
action=${1:-all}
platform=${2:?Supply windows-x64, macos-arm64, linux-x64 or linux-arm64}
build=${PAINT_BUILD_DIR:-.build/native/app}
jobs=${BUILD_JOBS:-2}
export CC=clang CXX=clang++ OBJCXX=clang++
export CCACHE_DIR="${CCACHE_DIR:-$PWD/.ccache}"
export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$PWD}"
export CCACHE_COMPILERCHECK=content
export CMAKE_C_COMPILER_LAUNCHER=ccache
export CMAKE_CXX_COMPILER_LAUNCHER=ccache
export CMAKE_OBJCXX_COMPILER_LAUNCHER=ccache
# The harness toolchain file (harness.lock pins it at template_harness/) reads
# the macOS LLVM runtime prefix from HARNESS_LLVM_RUNTIME.
if [ -n "${GUI_FORMS_LLVM_RUNTIME:-}" ]; then
  export HARNESS_LLVM_RUNTIME="${HARNESS_LLVM_RUNTIME:-$GUI_FORMS_LLVM_RUNTIME}"
fi
if command -v cygpath >/dev/null 2>&1; then
  CCACHE_DIR=$(cygpath -m "$CCACHE_DIR")
  CCACHE_BASEDIR=$(cygpath -m "$CCACHE_BASEDIR")
fi
ccache --max-size=500M
case "$action" in
  configure|all)
    compiler_c=$(command -v clang)
    compiler_cxx=$(command -v clang++)
    if command -v cygpath >/dev/null 2>&1; then
      compiler_c=$(cygpath -m "$compiler_c.exe")
      compiler_cxx=$(cygpath -m "$compiler_cxx.exe")
    fi
    python3 scripts/fetch-gui-forms.py
    # The toolkit takes STXlib (its text stack since 2026-11-16) and Skia
    # from its pinned stx source (cmake/Stx.cmake); nothing else is fetched.
    set -- -DRAINSTAR_TOOLKIT_SOURCE_DIR="$PWD/gui_forms"
    set -- "$@" -DPAINT_RELEASE_VERSION="${PAINT_RELEASE_VERSION:-$(python3 scripts/release_version.py)}"
    # The codecs are static archives from pinned sources (cmake/ImportFormats.cmake,
    # RAINSTAR_SYSTEM_CODECS OFF): nothing is installed or prebuilt for them.
    case "$platform" in
      windows-x64|macos-arm64|linux-x64|linux-arm64) ;;
      *) echo "Unsupported native platform: $platform" >&2; exit 1 ;;
    esac
    # Windows: GUI.Forms' own-runtime toolchain for x86_64-pc-windows-msvc
    # (gui_forms/tools/toolkit_configure.py toolchain, after own_runtime.py
    # build has set its locations). Elsewhere the harness's llvm22.cmake.
    if [ "$platform" = windows-x64 ]; then
      toolchain=$(python3 gui_forms/tools/toolkit_configure.py toolchain)
    else
      toolchain="-DCMAKE_TOOLCHAIN_FILE=$PWD/template_harness/harness/cmake/llvm22.cmake"
    fi
    # shellcheck disable=SC2086 # the toolchain options are words
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      $toolchain \
      -DCMAKE_C_COMPILER="$compiler_c" -DCMAKE_CXX_COMPILER="$compiler_cxx" \
      -DRAINSTAR_SYSTEM_CODECS=OFF "$@"
    ;;
esac
case "$action" in
  build|all) cmake --build "$build" --parallel "$jobs"; ccache --show-stats ;;
esac
case "$action" in
  test|all)
    ctest --test-dir "$build" --output-on-failure --timeout 180
    python3 scripts/check-style.py
    python3 -m unittest discover -s tests -p 'test_toolkit_cache.py'
    python3 -m unittest discover -s tests -p 'test_system_libraries.py'
    ;;
esac
case "$action" in
  package|all)
    # GUI.Forms is an EXCLUDE_FROM_ALL subdirectory: build its own targets (its
    # install set) and install it, for the notices the packages copy
    # (cmake/GUIFormsFrontend.cmake, paint_package_toolkit).
    cmake --build "$build" --target toolkit/all --parallel "$jobs"
    rm -rf "$build/gui-forms-sdk"
    cmake --install "$build/toolkit" --prefix "$build/gui-forms-sdk"
    cmake --build "$build" --target paint-package

    ;;
esac
case "$action" in configure|build|test|package|all) ;; *) echo "Unknown action: $action" >&2; exit 1 ;; esac


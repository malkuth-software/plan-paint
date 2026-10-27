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
    sh gui_forms/third_party/fetch_text_stack.sh
    set -- -DRAINSTAR_TOOLKIT_SOURCE_DIR="$PWD/gui_forms"
    set -- "$@" -DPAINT_RELEASE_VERSION="${PAINT_RELEASE_VERSION:-$(python3 scripts/release_version.py)}"
    if [ "$platform" = macos-arm64 ]; then
      sh scripts/build-macos-codecs.sh
      export PKG_CONFIG_PATH="$PWD/.build/codecs-macos14/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      set -- "$@" -DCMAKE_PREFIX_PATH="$PWD/.build/codecs-macos14${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
    fi
    case "$platform" in
      windows-x64) ;;
      macos-arm64|linux-x64|linux-arm64)
        set -- "$@" -DGUI_FORMS_SKIA_PREBUILT=ON \
          -DGUI_FORMS_SKIA_OUT="$PWD/gui_forms/third_party/skia/out/paint"
        ;;
      *) echo "Unsupported native platform: $platform" >&2; exit 1 ;;
    esac
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/gui_forms/cmake/llvm22.cmake" \
      -DCMAKE_C_COMPILER="$compiler_c" -DCMAKE_CXX_COMPILER="$compiler_cxx" \
      -DRAINSTAR_BUNDLED_TIFF=ON "$@"
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
    ;;
esac
case "$action" in
  package|all)
    cmake --build "$build" --target paint-package

    ;;
esac
case "$action" in configure|build|test|package|all) ;; *) echo "Unknown action: $action" >&2; exit 1 ;; esac


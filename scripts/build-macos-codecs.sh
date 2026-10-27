#!/bin/sh
# Build the external C codecs for the same minimum OS as the application.
set -eu
prefix="$PWD/.build/codecs-macos14"
if [ -f "$prefix/complete" ]; then exit 0; fi
sources="$PWD/.build/codec-sources"
jobs=${BUILD_JOBS:-2}
fetch_revision() {
  destination=$1
  remote=$2
  revision=$3
  if [ ! -d "$destination/.git" ]; then
    git init "$destination"
    git -C "$destination" remote add origin "$remote"
  fi
  git -C "$destination" fetch --depth=1 origin "$revision"
  git -C "$destination" checkout --detach "$revision"
}
fetch_revision "$sources/webp" https://chromium.googlesource.com/webm/libwebp \
  4fa21912338357f89e4fd51cf2368325b59e9bd9
fetch_revision "$sources/dav1d" https://github.com/videolan/dav1d.git \
  54706fc6bc0cdecab7e9593974a4039cc038fca7
export MACOSX_DEPLOYMENT_TARGET=14.0
cmake -S "$sources/webp" -B "$sources/webp-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_C_COMPILER="$(command -v clang)" -DCMAKE_CXX_COMPILER="$(command -v clang++)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DBUILD_SHARED_LIBS=ON \
  -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
  -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
cmake --build "$sources/webp-build" --parallel "$jobs"
cmake --install "$sources/webp-build"
set --
if [ -f "$sources/dav1d-build/meson-private/coredata.dat" ]; then set -- --reconfigure; fi
CC="ccache clang" CFLAGS="-mmacosx-version-min=14.0" LDFLAGS="-mmacosx-version-min=14.0" \
  meson setup "$@" "$sources/dav1d-build" "$sources/dav1d" \
    --prefix="$prefix" --libdir=lib --buildtype=release --default-library=shared \
    -Denable_tools=false -Denable_tests=false
meson compile -C "$sources/dav1d-build" -j "$jobs"
meson install -C "$sources/dav1d-build"
python3 gui_forms/tools/audit_macos_minimum.py "$prefix/lib"
touch "$prefix/complete"

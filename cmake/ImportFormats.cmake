# Pinned import-only dependencies. The application continues to export its
# native raster formats. Every codec is a static archive linked into the one
# program module (RAINSTAR_SYSTEM_CODECS OFF, the default): libtiff and
# libwebp are fetched at pinned commits and built here; dav1d is built by
# libavif's own LocalDav1d module at libavif v1.4.2's pin, which needs meson
# and ninja on the build host (and nasm for x86-64 assembly).
#
# Outputs: RAINSTAR_TIFF_TARGET, RAINSTAR_WEBP_TARGET (linked by paint_core),
# WEBP_INCLUDE_DIR (the directory holding webp/decode.h) and the avif target.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(RAINSTAR_SYSTEM_CODECS)
  message(WARNING "RAINSTAR_SYSTEM_CODECS links installed codec libraries; this program is not shippable.")
  find_package(PkgConfig QUIET)
  find_package(TIFF REQUIRED)
  set(RAINSTAR_TIFF_TARGET TIFF::TIFF)
  find_package(WebP CONFIG QUIET)
  if(TARGET WebP::webp)
    set(RAINSTAR_WEBP_TARGET WebP::webp)
  else()
    find_path(WEBP_INCLUDE_DIR webp/decode.h REQUIRED)
    find_library(WEBP_LIBRARY NAMES webp REQUIRED)
    set(RAINSTAR_WEBP_TARGET ${WEBP_LIBRARY})
  endif()
  set(AVIF_CODEC_DAV1D SYSTEM CACHE STRING "" FORCE)
else()
  # The reduced TIFF surface. win32-io would make libtiff's own sources see
  # thandle_t as a Win32 HANDLE while our TIFFClientOpen callbacks see void*:
  # a call through a mismatched function type. Plan Paint opens TIFF only
  # from memory.
  foreach(option tiff-tools tiff-tests tiff-contrib tiff-docs tiff-cxx tiff-install
                 tiff-opengl win32-io
                 ccitt packbits lzw thunder next logluv mdi zlib libdeflate pixarlog jpeg old-jpeg jpeg12
                 jbig lerc lzma zstd webp)
    set(${option} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(CMAKE_DISABLE_FIND_PACKAGE_Deflate ON CACHE BOOL "" FORCE)
  set(HAVE_JPEGTURBO_DUAL_MODE_8_12 OFF CACHE INTERNAL "" FORCE)
  set(JPEG_DUAL_MODE_8_12 OFF CACHE INTERNAL "" FORCE)
  set(tiff-static ON CACHE BOOL "" FORCE)
  # v4.7.2 (the tag object f1cabd54 points at this commit).
  FetchContent_Declare(tiff
    GIT_REPOSITORY https://gitlab.com/libtiff/libtiff.git
    GIT_TAG d01a94be176f5f6a87f7ee1c0b32e65416aa2b4d)
  FetchContent_MakeAvailable(tiff)
  set(RAINSTAR_TIFF_TARGET TIFF::tiff)

  # libwebp at the revision scripts/build-macos-codecs.sh built shared until
  # 2026-11-15; only the decoder library is used (src/codecs.cpp includes
  # webp/decode.h). The target "webp" is static because BUILD_SHARED_LIBS is
  # OFF above.
  foreach(option WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP WEBP_BUILD_GIF2WEBP
                 WEBP_BUILD_IMG2WEBP WEBP_BUILD_VWEBP WEBP_BUILD_WEBPINFO WEBP_BUILD_WEBPMUX
                 WEBP_BUILD_EXTRAS WEBP_BUILD_WEBP_JS WEBP_BUILD_FUZZTEST WEBP_BUILD_LIBWEBPMUX)
    set(${option} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(webp
    GIT_REPOSITORY https://chromium.googlesource.com/webm/libwebp
    GIT_TAG 4fa21912338357f89e4fd51cf2368325b59e9bd9)
  FetchContent_MakeAvailable(webp)
  set(RAINSTAR_WEBP_TARGET webp)
  set(WEBP_INCLUDE_DIR "${webp_SOURCE_DIR}/src")

  # LocalDav1d runs meson during configure; the environment carries the
  # minimum macOS version to its compiler, as CMAKE_OSX_DEPLOYMENT_TARGET does
  # for ours (template_harness/harness/audit_macos_minimum.py checks the
  # package).
  if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
  set(AVIF_CODEC_DAV1D LOCAL CACHE STRING "" FORCE)
endif()

foreach(codec AOM LIBGAV1 RAV1E SVT AVM)
  set(AVIF_CODEC_${codec} OFF CACHE STRING "" FORCE)
endforeach()
foreach(option AVIF_BUILD_APPS AVIF_BUILD_TESTS AVIF_BUILD_EXAMPLES AVIF_ENABLE_GTEST AVIF_LIBYUV AVIF_LIBSHARPYUV)
  set(${option} OFF CACHE STRING "" FORCE)
endforeach()
FetchContent_Declare(avif
  URL https://codeload.github.com/AOMediaCodec/libavif/tar.gz/refs/tags/v1.4.2
  URL_HASH SHA256=2b645287340ba5a631d268b551dc2d72bd73ac33335962dd36dcdb6d8366921d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(avif)

set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lunasvg
  URL https://codeload.github.com/sammycage/lunasvg/tar.gz/83c58df8103dc7dca423dfd824992af94d49bed6
  URL_HASH SHA256=37f051e6f95ca53d4d17aebb548eaea14536de87edaf105073260d5e5f6ffd1a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lunasvg)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinyxml2
  URL https://codeload.github.com/leethomason/tinyxml2/tar.gz/11.0.0
  URL_HASH SHA256=5556deb5081fb246ee92afae73efd943c889cef0cafea92b0b82422d6a18f289
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(tinyxml2)

# Pinned import-only dependencies. The application continues to export its native raster formats.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AVIF_CODEC_DAV1D SYSTEM CACHE STRING "" FORCE)
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

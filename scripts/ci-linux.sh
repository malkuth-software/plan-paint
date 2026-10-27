#!/bin/sh
set -eu
exec sh scripts/native-build.sh all linux-${PAINT_ARCH:-x64}

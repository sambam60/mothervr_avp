#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
SRC="$SCRIPT_DIR/src/recon_interpose.c"
WRAPPER_SRC="$SCRIPT_DIR/src/bundle_exec_wrapper.c"
OUT_DIR="$SCRIPT_DIR/build"
OUT_LIB="$OUT_DIR/libmothervr_avp_recon.dylib"
OUT_WRAPPER="$OUT_DIR/Alien Isolation.wrapper"

mkdir -p "$OUT_DIR"

clang \
  -arch x86_64 \
  -dynamiclib \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -DGL_SILENCE_DEPRECATION \
  -framework OpenGL \
  -framework AppKit \
  -o "$OUT_LIB" \
  "$SRC"

echo "Built $OUT_LIB"

clang \
  -arch arm64 \
  -arch x86_64 \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -o "$OUT_WRAPPER" \
  "$WRAPPER_SRC"

echo "Built $OUT_WRAPPER"

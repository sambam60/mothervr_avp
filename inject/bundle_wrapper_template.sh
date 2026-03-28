#!/bin/zsh
set -euo pipefail

APP_DIR=$(cd "$(dirname "$0")" && pwd)
GAME_ROOT=$(cd "$APP_DIR/../../.." && pwd)
REAL_BIN="$APP_DIR/Alien Isolation.real"
RECON_DYLIB="$GAME_ROOT/mothervr_avp/inject/build/libmothervr_avp_recon.dylib"
RECON_LOG="${MOTHERVR_AVP_LOG:-$GAME_ROOT/mothervr_avp/inject/build/recon.log}"
WRAPPER_LOG="$GAME_ROOT/mothervr_avp/inject/build/wrapper.log"

mkdir -p "$(dirname "$RECON_LOG")"

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] wrapper invoked"
  echo "cwd(before)=$(pwd)"
  echo "argv=$*"
} >> "$WRAPPER_LOG"

if [[ ! -x "$REAL_BIN" ]]; then
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] missing real binary: $REAL_BIN" >> "$WRAPPER_LOG"
  exit 1
fi

if [[ ! -f "$RECON_DYLIB" ]]; then
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] missing dylib: $RECON_DYLIB" >> "$WRAPPER_LOG"
  exec "$REAL_BIN" "$@"
fi

cd "$GAME_ROOT"
export DYLD_INSERT_LIBRARIES="$RECON_DYLIB"
export MOTHERVR_AVP_LOG="$RECON_LOG"

echo "[$(date '+%Y-%m-%d %H:%M:%S')] launching real binary with DYLD_INSERT_LIBRARIES=$RECON_DYLIB" >> "$WRAPPER_LOG"
exec "$REAL_BIN" "$@"

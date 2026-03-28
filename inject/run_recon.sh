#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
GAME_EXEC="$WORKSPACE_ROOT/Alien Isolation.app/Contents/MacOS/Alien Isolation"
GAME_DIR=$(dirname "$GAME_EXEC")
DYLIB_PATH="$SCRIPT_DIR/build/libmothervr_avp_recon.dylib"
LOG_PATH="${MOTHERVR_AVP_LOG:-$SCRIPT_DIR/build/recon.log}"
ROOT_APPID_FILE="$WORKSPACE_ROOT/steam_appid.txt"
LOCAL_APPID_FILE="$GAME_DIR/steam_appid.txt"

if [[ ! -f "$DYLIB_PATH" ]]; then
  echo "Missing $DYLIB_PATH"
  echo "Run $SCRIPT_DIR/build.sh first."
  exit 1
fi

if [[ ! -x "$GAME_EXEC" ]]; then
  echo "Missing game executable at $GAME_EXEC"
  exit 1
fi

mkdir -p "$(dirname "$LOG_PATH")"

if [[ -f "$ROOT_APPID_FILE" && ! -f "$LOCAL_APPID_FILE" ]]; then
  cp "$ROOT_APPID_FILE" "$LOCAL_APPID_FILE"
fi

# Keep the original Steam game root as cwd so sibling assets like
# `AlienIsolationData` remain discoverable, while still exposing
# `steam_appid.txt` next to the executable for SteamAPI bootstrap.
cd "$WORKSPACE_ROOT"
export DYLD_INSERT_LIBRARIES="$DYLIB_PATH"
export MOTHERVR_AVP_LOG="$LOG_PATH"

echo "Launching Alien: Isolation with recon dylib"
echo "Log: $LOG_PATH"
exec "$GAME_EXEC"

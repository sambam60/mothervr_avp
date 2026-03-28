#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
MACOS_DIR="$WORKSPACE_ROOT/Alien Isolation.app/Contents/MacOS"
LIVE_BIN="$MACOS_DIR/Alien Isolation"
REAL_BIN="$MACOS_DIR/Alien Isolation.real"
WRAPPER_BIN="$SCRIPT_DIR/build/Alien Isolation.wrapper"
ROOT_APPID_FILE="$WORKSPACE_ROOT/steam_appid.txt"
LOCAL_APPID_FILE="$MACOS_DIR/steam_appid.txt"

if [[ ! -f "$WRAPPER_BIN" ]]; then
  echo "Missing wrapper binary: $WRAPPER_BIN"
  echo "Run $SCRIPT_DIR/build.sh first."
  exit 1
fi

if [[ ! -x "$LIVE_BIN" && ! -x "$REAL_BIN" ]]; then
  echo "Could not find Alien: Isolation binary in $MACOS_DIR"
  exit 1
fi

if [[ -x "$REAL_BIN" ]]; then
  echo "Bundle wrapper already installed."
else
  mv "$LIVE_BIN" "$REAL_BIN"
  cp "$WRAPPER_BIN" "$LIVE_BIN"
  chmod +x "$LIVE_BIN"
  echo "Installed wrapper at $LIVE_BIN"
fi

if [[ -f "$ROOT_APPID_FILE" && ! -f "$LOCAL_APPID_FILE" ]]; then
  cp "$ROOT_APPID_FILE" "$LOCAL_APPID_FILE"
  echo "Copied steam_appid.txt into app executable directory."
fi

echo "Done. Launch the game normally from Steam."

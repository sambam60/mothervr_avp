#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
MACOS_DIR="$WORKSPACE_ROOT/Alien Isolation.app/Contents/MacOS"
LIVE_BIN="$MACOS_DIR/Alien Isolation"
REAL_BIN="$MACOS_DIR/Alien Isolation.real"

if [[ ! -x "$REAL_BIN" ]]; then
  echo "Bundle wrapper is not installed."
  exit 0
fi

rm -f "$LIVE_BIN"
mv "$REAL_BIN" "$LIVE_BIN"

echo "Restored original Alien: Isolation binary."

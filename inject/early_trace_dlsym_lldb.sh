#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TRACE_LOG="$SCRIPT_DIR/build/early_dlsym_trace.log"
TRACE_DURATION_SECONDS="${MOTHERVR_EARLY_DLSYM_TRACE_SECONDS:-30}"

mkdir -p "$(dirname "$TRACE_LOG")"

PID=""
for _ in {1..120}; do
  PID=$(python3 - <<'PY'
import subprocess

needle = "Alien Isolation.app/Contents/MacOS/Alien Isolation"
result = subprocess.run(
    ["ps", "-ax", "-o", "pid=,command="],
    capture_output=True,
    text=True,
    check=True,
)
for line in result.stdout.splitlines():
    parts = line.strip().split(None, 1)
    if len(parts) == 2 and parts[1].endswith(needle):
        print(parts[0])
        break
PY
)
  if [[ -n "$PID" ]]; then
    break
  fi
  sleep 1
done

if [[ -z "$PID" ]]; then
  echo "Timed out waiting for Alien: Isolation to start."
  exit 1
fi

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] dlsym trace attaching to pid $PID"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] trace duration ${TRACE_DURATION_SECONDS}s"
} >> "$TRACE_LOG"

LLDB_CMDS=$(mktemp)
PY_HELPER_DIR=$(mktemp -d)
PY_HELPER="$PY_HELPER_DIR/mothervr_dlsym_trace_helper.py"
MODULE_NAME="mothervr_dlsym_trace_helper"
cleanup() {
  rm -f "$LLDB_CMDS"
  rm -f "$PY_HELPER"
  rmdir "$PY_HELPER_DIR" 2>/dev/null || true
}
trap cleanup EXIT

cat > "$PY_HELPER" <<EOF
import lldb
import threading
import time

_mothervr_log = open(r'''$TRACE_LOG''', 'a')
_mothervr_counts = {}

def _mothervr_dlsym_trace(frame, bp_loc, internal_dict):
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    name_ptr = frame.FindRegister("rsi").GetValueAsUnsigned()
    symbol = process.ReadCStringFromMemory(name_ptr, 512, error) if name_ptr else None
    if not symbol:
        symbol = "<null>"
    _mothervr_counts[symbol] = _mothervr_counts.get(symbol, 0) + 1
    if _mothervr_counts[symbol] == 1:
        _mothervr_log.write(f"[first] {symbol}\\n")
        _mothervr_log.flush()
    return False

def start_timer(seconds):
    def _stop_later():
        time.sleep(seconds)
        process = lldb.debugger.GetSelectedTarget().GetProcess()
        if process and process.IsValid() and process.GetState() == lldb.eStateRunning:
            process.Stop()
    threading.Thread(target=_stop_later, daemon=True).start()

def write_summary():
    _mothervr_log.write("[summary]\\n")
    for symbol, count in sorted(_mothervr_counts.items(), key=lambda item: (-item[1], item[0])):
        _mothervr_log.write(f"{count} {symbol}\\n")
    _mothervr_log.flush()
EOF

cat > "$LLDB_CMDS" <<EOF
command script import "$PY_HELPER"
breakpoint set -s libdyld.dylib -n dlsym
breakpoint modify --auto-continue true 1
breakpoint command add -F ${MODULE_NAME}._mothervr_dlsym_trace 1
script ${MODULE_NAME}.start_timer($TRACE_DURATION_SECONDS)
process continue
script ${MODULE_NAME}.write_summary()
breakpoint list
detach
quit
EOF

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach start"
  lldb --batch --attach-pid "$PID" --source "$LLDB_CMDS"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach end"
} >> "$TRACE_LOG" 2>&1

echo "dlsym trace complete. See $TRACE_LOG"

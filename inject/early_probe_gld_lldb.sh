#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROBE_LOG="$SCRIPT_DIR/build/early_probe_gld.log"
PROBE_DURATION_SECONDS="${MOTHERVR_EARLY_PROBE_GLD_SECONDS:-30}"

mkdir -p "$(dirname "$PROBE_LOG")"

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
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] early gld probe attaching to pid $PID"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] probe duration ${PROBE_DURATION_SECONDS}s"
} >> "$PROBE_LOG"

LLDB_CMDS=$(mktemp)
cleanup() {
  rm -f "$LLDB_CMDS"
}
trap cleanup EXIT

cat > "$LLDB_CMDS" <<EOF
script lldb.debugger.SetAsync(True)
breakpoint set -n gldPopulateContextDispatch
breakpoint set -n gldUpdateDispatch
breakpoint set -n gldBufferSubData
breakpoint set -n gldCopyBufferSubData
breakpoint set -n gldPresentFramebufferData
breakpoint set -n gldFlushContext
breakpoint set -n gliCreateContext
breakpoint set -n gliAttachDrawable
breakpoint modify --auto-continue true 1 2 3 4 5 6 7 8
process continue
script import time; time.sleep(${PROBE_DURATION_SECONDS})
process interrupt
breakpoint list
detach
quit
EOF

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach start"
  lldb --batch --attach-pid "$PID" --source "$LLDB_CMDS"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach end"
} >> "$PROBE_LOG" 2>&1

echo "Early gld probe complete. See $PROBE_LOG"

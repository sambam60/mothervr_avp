#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROBE_LOG="$SCRIPT_DIR/build/probe_gl_calls.log"
PROBE_DURATION_SECONDS="${MOTHERVR_PROBE_DURATION_SECONDS:-15}"
ATTACH_DELAY_SECONDS="${MOTHERVR_PROBE_ATTACH_DELAY_SECONDS:-8}"

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
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] probing pid $PID for ${PROBE_DURATION_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] sleeping ${ATTACH_DELAY_SECONDS}s before attach"
} >> "$PROBE_LOG"

sleep "$ATTACH_DELAY_SECONDS"

LLDB_CMDS=$(mktemp)
cleanup() {
  rm -f "$LLDB_CMDS"
}
trap cleanup EXIT

cat > "$LLDB_CMDS" <<EOF
settings set interpreter.async true
breakpoint set -n glBindBuffer
breakpoint set -n glBufferSubData
breakpoint set -n glProgramUniform4fv
breakpoint set -n CGLFlushDrawable
breakpoint set -n dlsym
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

echo "Probe complete. See $PROBE_LOG"

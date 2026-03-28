#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DYLIB_PATH="$SCRIPT_DIR/build/libmothervr_avp_recon.dylib"
ATTACH_LOG="$SCRIPT_DIR/build/attach.log"
LOG_PATH="$SCRIPT_DIR/build/recon.log"
WAIT_FOR_PROCESS_SECONDS="${MOTHERVR_WAIT_FOR_PROCESS_SECONDS:-600}"
MIN_PROCESS_AGE_SECONDS="${MOTHERVR_MIN_PROCESS_AGE_SECONDS:-0}"
ATTACH_DELAY_SECONDS="${MOTHERVR_ATTACH_DELAY_SECONDS:-2}"
RECON_LOG_WAIT_SECONDS="${MOTHERVR_RECON_LOG_WAIT_SECONDS:-5}"

if [[ ! -f "$DYLIB_PATH" ]]; then
  echo "Missing $DYLIB_PATH"
  echo "Run $SCRIPT_DIR/build.sh first."
  exit 1
fi

mkdir -p "$(dirname "$ATTACH_LOG")"
rm -f "$LOG_PATH"

find_game_pid() {
  python3 - "$MIN_PROCESS_AGE_SECONDS" <<'PY'
import subprocess
import sys


def parse_etime(value: str) -> int:
    value = value.strip()
    if not value:
        return 0
    days = 0
    if "-" in value:
        day_part, value = value.split("-", 1)
        days = int(day_part)
    parts = [int(part) for part in value.split(":")]
    if len(parts) == 2:
        hours = 0
        minutes, seconds = parts
    elif len(parts) == 3:
        hours, minutes, seconds = parts
    else:
        return 0
    return days * 86400 + hours * 3600 + minutes * 60 + seconds

needles = [
    "Alien Isolation.app/Contents/MacOS/Alien Isolation",
    "Alien Isolation.real",
]
min_age_seconds = int(sys.argv[1])
result = subprocess.run(
    ["ps", "-ax", "-o", "pid=,etime=,command="],
    capture_output=True,
    text=True,
    check=True,
)
matches = []
for line in result.stdout.splitlines():
    parts = line.strip().split(None, 2)
    if len(parts) != 3:
        continue
    pid, etime, command = parts
    if "Cursor Helper" in command:
        continue
    if any(needle in command for needle in needles) and parse_etime(etime) >= min_age_seconds:
        matches.append((int(pid), command))

if matches:
    matches.sort()
    print(matches[-1][0])
PY
}

log_matching_processes() {
  python3 - <<'PY'
import subprocess

result = subprocess.run(
    ["ps", "-ax", "-o", "pid=,etime=,command="],
    capture_output=True,
    text=True,
    check=True,
)
for line in result.stdout.splitlines():
    if "Alien Isolation" in line or "steam_osx" in line:
        print(line)
PY
}

wait_for_recon_log() {
  local waited=0
  while (( waited < RECON_LOG_WAIT_SECONDS )); do
    if [[ -s "$LOG_PATH" ]]; then
      return 0
    fi
    sleep 1
    ((waited += 1))
  done

  [[ -s "$LOG_PATH" ]]
}

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach helper starting"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] wait timeout ${WAIT_FOR_PROCESS_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] minimum process age ${MIN_PROCESS_AGE_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach delay ${ATTACH_DELAY_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] dylib=${DYLIB_PATH}"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] expected log path=${LOG_PATH}"
} >> "$ATTACH_LOG"

PID=""
WAIT_ITERATIONS=$((WAIT_FOR_PROCESS_SECONDS))
for ((i = 0; i < WAIT_ITERATIONS; i++)); do
  PID=$(find_game_pid)
  if [[ -n "$PID" ]]; then
    break
  fi
  sleep 1
done

if [[ -z "$PID" ]]; then
  {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] timed out waiting for Alien: Isolation to start"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] visible matching processes:"
    log_matching_processes
  } >> "$ATTACH_LOG"
  echo "Timed out waiting for Alien: Isolation to start."
  exit 1
fi

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] found pid $PID"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] sleeping ${ATTACH_DELAY_SECONDS}s before attach"
} >> "$ATTACH_LOG"

sleep "$ATTACH_DELAY_SECONDS"

LATEST_PID=$(find_game_pid)
if [[ -n "$LATEST_PID" && "$LATEST_PID" != "$PID" ]]; then
  {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] pid changed from $PID to $LATEST_PID before attach"
  } >> "$ATTACH_LOG"
  PID="$LATEST_PID"
fi

LLDB_CMDS=$(mktemp)
cleanup() {
  rm -f "$LLDB_CMDS"
}
trap cleanup EXIT

cat > "$LLDB_CMDS" <<EOF2
expr -- (void*)dlopen("$DYLIB_PATH", 2)
detach
quit
EOF2

ATTACH_SUCCESS=0
for attempt in 1 2 3; do
  LATEST_PID=$(find_game_pid)
  if [[ -n "$LATEST_PID" ]]; then
    PID="$LATEST_PID"
  fi

  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach attempt ${attempt} pid $PID" >> "$ATTACH_LOG"

  if lldb --batch --attach-pid "$PID" --source "$LLDB_CMDS" >> "$ATTACH_LOG" 2>&1; then
    if wait_for_recon_log; then
      ATTACH_SUCCESS=1
      break
    fi

    {
      echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach attempt ${attempt} produced no recon log after ${RECON_LOG_WAIT_SECONDS}s"
      echo "[$(date '+%Y-%m-%d %H:%M:%S')] visible matching processes:"
      log_matching_processes
    } >> "$ATTACH_LOG"
    sleep 2
    continue
  fi

  {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach attempt ${attempt} failed"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] visible matching processes:"
    log_matching_processes
  } >> "$ATTACH_LOG"
  sleep 2
done

if [[ "$ATTACH_SUCCESS" -ne 1 ]]; then
  echo "Injection attempt failed. See:"
  echo "  $ATTACH_LOG"
  exit 1
fi

echo "Injection attempt complete. See:"
echo "  $ATTACH_LOG"
echo "  $LOG_PATH"

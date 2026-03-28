#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TRACE_LOG="$SCRIPT_DIR/build/target_gld_address_trace.log"
RAW_SAMPLE_PATH="$SCRIPT_DIR/build/target_gld_address_trace.sample.txt"
TRACE_DURATION_SECONDS="${MOTHERVR_TARGET_GLD_TRACE_SECONDS:-180}"
SAMPLE_INTERVAL_MS="${MOTHERVR_SAMPLE_INTERVAL_MS:-20}"
WAIT_FOR_PROCESS_SECONDS="${MOTHERVR_WAIT_FOR_PROCESS_SECONDS:-600}"
ATTACH_DELAY_SECONDS="${MOTHERVR_ATTACH_DELAY_SECONDS:-0}"
MIN_PROCESS_AGE_SECONDS="${MOTHERVR_MIN_PROCESS_AGE_SECONDS:-6}"
TRACE_SYMBOLS_CSV="${MOTHERVR_TRACE_SYMBOLS:-gldUpdateDispatch,gldPresentFramebufferData,gliAttachDrawable,gldFlushContext}"

mkdir -p "$(dirname "$TRACE_LOG")"

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

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] target gld sampling trace starting"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] waiting for Alien: Isolation process"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] wait timeout ${WAIT_FOR_PROCESS_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] attach delay ${ATTACH_DELAY_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] minimum process age ${MIN_PROCESS_AGE_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] trace duration ${TRACE_DURATION_SECONDS}s"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] sample interval ${SAMPLE_INTERVAL_MS}ms"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] trace symbols ${TRACE_SYMBOLS_CSV}"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] raw sample path ${RAW_SAMPLE_PATH}"
} >> "$TRACE_LOG"

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
  } >> "$TRACE_LOG"
  echo "Timed out waiting for Alien: Isolation to start."
  exit 1
fi

{
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] target gld sampling pid $PID"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] sleeping ${ATTACH_DELAY_SECONDS}s before sampling"
} >> "$TRACE_LOG"

sleep "$ATTACH_DELAY_SECONDS"

LATEST_PID=$(find_game_pid)
if [[ -n "$LATEST_PID" && "$LATEST_PID" != "$PID" ]]; then
  {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] pid changed from $PID to $LATEST_PID before sampling"
  } >> "$TRACE_LOG"
  PID="$LATEST_PID"
fi

rm -f "$RAW_SAMPLE_PATH"

if sample "$PID" "$TRACE_DURATION_SECONDS" "$SAMPLE_INTERVAL_MS" -mayDie -file "$RAW_SAMPLE_PATH" >> "$TRACE_LOG" 2>&1; then
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] sample completed" >> "$TRACE_LOG"
else
  {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] sample command returned non-zero"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] visible matching processes:"
    log_matching_processes
  } >> "$TRACE_LOG"
fi

python3 - "$RAW_SAMPLE_PATH" "$TRACE_LOG" "$TRACE_SYMBOLS_CSV" <<'PY'
from pathlib import Path
import sys

sample_path = Path(sys.argv[1])
log_path = Path(sys.argv[2])
symbols = [item.strip() for item in sys.argv[3].split(",") if item.strip()]

with log_path.open("a", encoding="utf-8") as log:
    log.write("[summary]\n")
    if not sample_path.exists():
        log.write("raw sample file missing\n")
        sys.exit(0)

    text = sample_path.read_text(encoding="utf-8", errors="replace")
    for symbol in symbols:
        count = text.count(symbol)
        log.write(f"{symbol}: stack_mentions={count}\n")

    log.write("[first-matches]\n")
    lines = text.splitlines()
    for symbol in symbols:
        log.write(f"{symbol}:\n")
        matches = [line for line in lines if symbol in line][:5]
        if matches:
            for line in matches:
                log.write(f"  {line}\n")
        else:
            log.write("  <none>\n")
PY

echo "[$(date '+%Y-%m-%d %H:%M:%S')] sampling end" >> "$TRACE_LOG"
echo "Target gld sampling trace complete. See $TRACE_LOG"

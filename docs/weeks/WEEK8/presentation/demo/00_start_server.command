#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR"
while [[ "$ROOT_DIR" != "/" && ! -f "$ROOT_DIR/CMakeLists.txt" ]]; do
  ROOT_DIR="$(dirname "$ROOT_DIR")"
done
if [[ ! -f "$ROOT_DIR/CMakeLists.txt" ]]; then
  echo "ERROR: project root(CMakeLists.txt) not found."
  read -r -p "Press Enter to close..."
  exit 1
fi

PORT=8080
PID_FILE="/tmp/week8_demo_server.pid"
LOG_FILE="/tmp/week8_demo_server.log"

cd "$ROOT_DIR"

echo "== WEEK8 Demo: Start Server =="
echo "root: $ROOT_DIR"

if [[ -f "$PID_FILE" ]]; then
  OLD_PID="$(cat "$PID_FILE" || true)"
  if [[ -n "${OLD_PID}" ]] && kill -0 "$OLD_PID" >/dev/null 2>&1; then
    echo "existing server pid=$OLD_PID found, stopping it first..."
    kill "$OLD_PID" >/dev/null 2>&1 || true
    sleep 1
  fi
fi

echo "[1/3] build week8_api_server"
cmake -S . -B build >/dev/null
cmake --build build -j4 --target week8_api_server >/dev/null

echo "[2/3] prepare demo dataset"
python3 - "$ROOT_DIR" <<'PY'
import csv
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
data = root / "data"
data.mkdir(parents=True, exist_ok=True)

small = data / "week8_bench_small.csv"
with small.open("w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["id", "name", "email"])
    for i in range(1, 101):
        w.writerow([i, f"s{i}", f"s{i}@x.com"])
PY

echo "[3/3] start server (pool + fixed timeout)"
W8_DISPATCH_MODE=pool \
W8_TIMEOUT_POLICY=fixed \
W8_FIXED_TIMEOUT_MS=3 \
W8_WORKER_COUNT=2 \
W8_QUEUE_CAPACITY=16 \
./build/week8_api_server >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > "$PID_FILE"
sleep 1

echo "server started: pid=$SERVER_PID"
echo "health check:"
curl -s "http://127.0.0.1:${PORT}/health" || true
echo
echo "server config: dispatch=pool timeout=fixed(3ms) workers=2 queue=16"
echo "log: $LOG_FILE"
echo "pid: $PID_FILE"
read -r -p "Press Enter to close..."

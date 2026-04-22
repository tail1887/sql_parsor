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

cd "$ROOT_DIR"

echo "== WEEK8 Demo: Overload Test =="
echo "scenario=burst requests=5000 concurrency=512"

RAW_RESULT="$(python3 scripts/week8/bench_client_02_deep.py \
  --scenario burst \
  --requests 5000 \
  --concurrency 512 \
  --port "${PORT}" || true)"

echo
echo "[RAW JSON]"
echo "$RAW_RESULT"

if [[ -z "$RAW_RESULT" ]]; then
  echo
  echo "[ERROR] empty benchmark output"
  read -r -p "Press Enter to close..."
  exit 1
fi

echo
echo "[SUMMARY]"
python3 - "$RAW_RESULT" <<'PY'
import json
import sys

raw = sys.argv[1]
try:
    obj = json.loads(raw)
except json.JSONDecodeError:
    print("invalid benchmark JSON")
    print(raw)
    raise SystemExit(0)

print(f"requests: {obj.get('requests')}")
print(f"concurrency: {obj.get('concurrency')}")
print(f"throughput_rps: {obj.get('throughput_rps', 0):.2f}")
print(f"p95_ms: {obj.get('p95_ms', 0):.3f}")
print(f"p99_ms: {obj.get('p99_ms', 0):.3f}")
print(f"success_ratio: {obj.get('success_ratio', 0):.4f}")
print(f"error_503_ratio: {obj.get('error_503_ratio', 0):.4f}")
print(f"error_504_ratio: {obj.get('error_504_ratio', 0):.4f}")
print()
print("[STATUS COUNTS]")
status_counts = obj.get("status_counts", {}) or {}
for code in sorted(status_counts, key=lambda x: int(x) if str(x).isdigit() else str(x)):
    print(f"{code}: {status_counts[code]}")
PY

echo
echo "expected: error_503_ratio > 0 (and usually error_504_ratio > 0 under heavy saturation)"
read -r -p "Press Enter to close..."

#!/bin/bash
set -euo pipefail

PORT=8080

echo "== WEEK8 Demo: Normal Query =="
echo "sending POST /query ..."

RAW_RESPONSE="$(curl -sS -w $'\n%{http_code}' -X POST "http://127.0.0.1:${PORT}/query" \
  -H "Content-Type: application/json" \
  -d '{"sql":"SELECT * FROM week8_bench_small;"}' || true)"

HTTP_CODE="$(printf '%s\n' "$RAW_RESPONSE" | awk 'END{print}')"
BODY="$(printf '%s\n' "$RAW_RESPONSE" | sed '$d')"

echo
echo "[HTTP STATUS]"
echo "$HTTP_CODE"

if [[ -z "$BODY" ]]; then
  echo
  echo "[ERROR] empty response body"
  read -r -p "Press Enter to close..."
  exit 1
fi

echo
echo "[SUMMARY]"
python3 - "$BODY" <<'PY'
import json
import sys

raw = sys.argv[1]
try:
    obj = json.loads(raw)
except json.JSONDecodeError:
    print("invalid JSON response")
    print(raw)
    raise SystemExit(0)

print(f"ok: {obj.get('ok')}")
meta = obj.get("metadata") or {}
print(f"endpoint: {meta.get('endpoint')}")

data = obj.get("data") or {}
output = data.get("output", "")
lines = output.splitlines()
print(f"rows(including header): {len(lines)}")
print()
print("[OUTPUT PREVIEW: first 8 lines]")
for line in lines[:8]:
    print(line)
if len(lines) > 8:
    print("... (truncated)")
PY

echo
echo "expected: HTTP 200 + ok:true + rows > 1"
read -r -p "Press Enter to close..."

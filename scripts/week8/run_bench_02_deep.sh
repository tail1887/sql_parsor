#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="${ROOT_DIR}/artifacts/week8/bench_02_deep"
CSV_PATH="${ART_DIR}/benchmark_results_02_deep.csv"
LOG_DIR="${ART_DIR}/logs"
PORT=8080
RUNS="${RUNS:-10}"

mkdir -p "${ART_DIR}" "${LOG_DIR}"

cat > "${CSV_PATH}" <<'EOF'
scenario,policy,run,requests,concurrency,throughput_rps,p95_ms,p99_ms,error_503_ratio,error_504_ratio,success_ratio
EOF

prepare_data() {
  python3 - "${ROOT_DIR}" <<'PY'
import csv
import pathlib
import random
import sys

root = pathlib.Path(sys.argv[1])
data_dir = root / "data"
data_dir.mkdir(parents=True, exist_ok=True)

small = data_dir / "week8_bench_small.csv"
medium = data_dir / "week8_bench_medium.csv"

with small.open("w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["id", "name", "email"])
    for i in range(1, 101):
        w.writerow([i, f"s{i}", f"s{i}@x.com"])

with medium.open("w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["id", "name", "email"])
    for i in range(1, 3001):
        w.writerow([i, f"m{i}", f"m{i}@x.com"])
PY
}

wait_until_ready() {
  local tries=0
  while [[ ${tries} -lt 80 ]]; do
    if curl -s "http://127.0.0.1:${PORT}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
    tries=$((tries + 1))
  done
  return 1
}

run_case() {
  local scenario="$1"
  local policy="$2"
  local run_no="$3"
  local requests="$4"
  local concurrency="$5"
  local out_json

  out_json="$(
    python3 "${ROOT_DIR}/scripts/week8/bench_client_02_deep.py" \
      --scenario "${scenario}" \
      --requests "${requests}" \
      --concurrency "${concurrency}" \
      --port "${PORT}" \
      --timeout-sec 4.0
  )"

  python3 - "$scenario" "$policy" "$run_no" "$out_json" "${CSV_PATH}" <<'PY'
import csv
import json
import sys

scenario = sys.argv[1]
policy = sys.argv[2]
run_no = int(sys.argv[3])
payload = json.loads(sys.argv[4])
csv_path = sys.argv[5]

row = {
    "scenario": scenario,
    "policy": policy,
    "run": run_no,
    "requests": payload["requests"],
    "concurrency": payload["concurrency"],
    "throughput_rps": f'{payload["throughput_rps"]:.2f}',
    "p95_ms": f'{payload["p95_ms"]:.2f}',
    "p99_ms": f'{payload["p99_ms"]:.2f}',
    "error_503_ratio": f'{payload["error_503_ratio"]:.4f}',
    "error_504_ratio": f'{payload["error_504_ratio"]:.4f}',
    "success_ratio": f'{payload["success_ratio"]:.4f}',
}

with open(csv_path, "a", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "scenario",
            "policy",
            "run",
            "requests",
            "concurrency",
            "throughput_rps",
            "p95_ms",
            "p99_ms",
            "error_503_ratio",
            "error_504_ratio",
            "success_ratio",
        ],
    )
    writer.writerow(row)
PY
}

run_policy() {
  local mode="$1"
  local policy="$2"
  local server_log="${LOG_DIR}/server_${policy}.log"

  prepare_data
  W8_DISPATCH_MODE="${mode}" "${ROOT_DIR}/build/week8_api_server" >"${server_log}" 2>&1 &
  local pid=$!

  if ! wait_until_ready; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
    echo "server did not become ready for ${policy}" >&2
    exit 1
  fi

  python3 "${ROOT_DIR}/scripts/week8/bench_client_02_deep.py" \
    --scenario normal --requests 80 --concurrency 8 --port "${PORT}" >/dev/null

  for run_no in $(seq 1 "${RUNS}"); do
    run_case normal "${policy}" "${run_no}" 1200 32
    run_case burst "${policy}" "${run_no}" 5000 192
    run_case saturation "${policy}" "${run_no}" 7000 320
  done

  kill "${pid}" >/dev/null 2>&1 || true
  wait "${pid}" 2>/dev/null || true
}

run_policy pool pool
run_policy per_request per_request

echo "deep benchmark CSV generated: ${CSV_PATH}"

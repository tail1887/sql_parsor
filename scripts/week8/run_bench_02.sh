#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="${ROOT_DIR}/artifacts/week8/bench_02"
CSV_PATH="${ART_DIR}/benchmark_results_02.csv"
LOG_DIR="${ART_DIR}/logs"
PORT=8080

mkdir -p "${ART_DIR}" "${LOG_DIR}"

cat > "${CSV_PATH}" <<'EOF'
scenario,policy,run,requests,concurrency,throughput_rps,p95_ms,error_503_ratio
EOF

wait_until_ready() {
  local tries=0
  while [[ ${tries} -lt 50 ]]; do
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
    python3 "${ROOT_DIR}/scripts/week8/bench_client_02.py" \
      --requests "${requests}" \
      --concurrency "${concurrency}" \
      --port "${PORT}"
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
    "error_503_ratio": f'{payload["error_503_ratio"]:.4f}',
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
            "error_503_ratio",
        ],
    )
    writer.writerow(row)
PY
}

run_policy() {
  local mode="$1"
  local policy="$2"
  local pid_file="${LOG_DIR}/server_${policy}.pid"
  local server_log="${LOG_DIR}/server_${policy}.log"

  W8_DISPATCH_MODE="${mode}" "${ROOT_DIR}/build/week8_api_server" >"${server_log}" 2>&1 &
  local pid=$!
  echo "${pid}" > "${pid_file}"

  if ! wait_until_ready; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
    echo "server did not become ready: ${policy}" >&2
    exit 1
  fi

  python3 "${ROOT_DIR}/scripts/week8/bench_client_02.py" --requests 50 --concurrency 8 --port "${PORT}" >/dev/null

  for run_no in 1 2 3; do
    run_case normal "${policy}" "${run_no}" 300 12
    run_case burst "${policy}" "${run_no}" 1200 64
    run_case saturation "${policy}" "${run_no}" 2400 160
  done

  kill "${pid}" >/dev/null 2>&1 || true
  wait "${pid}" 2>/dev/null || true
}

run_policy pool pool
run_policy per_request per_request

echo "benchmark CSV generated: ${CSV_PATH}"

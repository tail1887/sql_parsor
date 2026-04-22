#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="${ROOT_DIR}/artifacts/week8/bench_06"
CSV_PATH="${ART_DIR}/benchmark_results_06.csv"
SUMMARY_PATH="${ART_DIR}/summary_06.md"
LOG_DIR="${ART_DIR}/logs"
PORT=8080
RUNS="${RUNS:-8}"

mkdir -p "${ART_DIR}" "${LOG_DIR}"

cat > "${CSV_PATH}" <<'EOF'
scenario,policy,run,requests,concurrency,throughput_rps,p95_ms,p99_ms,error_503_ratio,error_504_ratio,success_ratio
EOF

prepare_data() {
  python3 - "${ROOT_DIR}" <<'PY'
import csv
import pathlib
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
  while [[ ${tries} -lt 100 ]]; do
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
  local timeout_policy="$1"
  local policy_label="$2"
  local fixed_ms="$3"
  local server_log="${LOG_DIR}/server_${policy_label}.log"

  prepare_data
  W8_DISPATCH_MODE="pool" \
  W8_TIMEOUT_POLICY="${timeout_policy}" \
  W8_FIXED_TIMEOUT_MS="${fixed_ms}" \
  "${ROOT_DIR}/build/week8_api_server" >"${server_log}" 2>&1 &
  local pid=$!

  if ! wait_until_ready; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
    echo "server did not become ready for ${policy_label}" >&2
    exit 1
  fi

  python3 "${ROOT_DIR}/scripts/week8/bench_client_02_deep.py" \
    --scenario normal --requests 80 --concurrency 8 --port "${PORT}" >/dev/null

  for run_no in $(seq 1 "${RUNS}"); do
    run_case normal "${policy_label}" "${run_no}" 1200 32
    run_case burst "${policy_label}" "${run_no}" 5000 192
    run_case saturation "${policy_label}" "${run_no}" 7000 320
  done

  kill "${pid}" >/dev/null 2>&1 || true
  wait "${pid}" 2>/dev/null || true
}

run_policy fixed fixed 10
run_policy dynamic dynamic 1200

python3 - "${CSV_PATH}" "${SUMMARY_PATH}" <<'PY'
import pandas as pd
import sys

csv_path = sys.argv[1]
summary_path = sys.argv[2]

df = pd.read_csv(csv_path)
for col in ["throughput_rps","p95_ms","p99_ms","error_503_ratio","error_504_ratio","success_ratio"]:
    df[col] = pd.to_numeric(df[col], errors="coerce")
df = df.dropna()

agg = (
    df.groupby(["scenario","policy"], as_index=False)
      .agg(
        throughput_mean=("throughput_rps","mean"),
        p95_mean=("p95_ms","mean"),
        p99_mean=("p99_ms","mean"),
        error503_mean=("error_503_ratio","mean"),
        error504_mean=("error_504_ratio","mean"),
        success_mean=("success_ratio","mean"),
      )
      .sort_values(["scenario","policy"])
)

lines = [
    "# Bench 06 Summary",
    "",
    "| scenario | policy | throughput_mean | p95_mean | p99_mean | 503_mean | 504_mean | success_mean |",
    "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for _, r in agg.iterrows():
    lines.append(
        f"| {r['scenario']} | {r['policy']} | {r['throughput_mean']:.2f} | {r['p95_mean']:.2f} | "
        f"{r['p99_mean']:.2f} | {r['error503_mean']:.4f} | {r['error504_mean']:.4f} | {r['success_mean']:.4f} |"
    )

with open(summary_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
PY

echo "bench_06 CSV: ${CSV_PATH}"
echo "bench_06 summary: ${SUMMARY_PATH}"

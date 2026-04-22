# Bench 02 Design (A vs B)

## Fixed Policies
- `A`: `W8_DISPATCH_MODE=pool` (fixed worker pool + bounded queue)
- `B`: `W8_DISPATCH_MODE=per_request` (one thread per accepted request)

## Fixed Scenarios
- `normal`: requests=300, concurrency=12
- `burst`: requests=1200, concurrency=64
- `saturation`: requests=2400, concurrency=160

## Fixed Request
- Method: `GET /health`
- Host: `127.0.0.1:8080`

## Fixed Measurement Rules
- Warm-up: 50 requests, concurrency 8
- Main runs: 3 runs per scenario/policy
- Metrics:
  - `throughput_rps = total_requests / elapsed_sec`
  - `p95_ms` from per-request latency distribution
  - `error_503_ratio = count(status=503) / total_requests`

## Fixed CSV Schema
`scenario,policy,run,requests,concurrency,throughput_rps,p95_ms,error_503_ratio`

## Fairness Checklist
- Same machine and no concurrent workload
- Same binary build for A/B
- Same request payload and scenario order
- Keep raw run logs for recalculation

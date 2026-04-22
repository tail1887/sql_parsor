#!/usr/bin/env python3
import argparse
import json
import random
import socket
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


def build_request(request_type: str, idx: int) -> bytes:
    if request_type == "health":
        return (
            "GET /health HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("utf-8")

    if request_type == "select_small":
        sql = "SELECT * FROM week8_bench_small;"
    elif request_type == "select_medium":
        sql = "SELECT * FROM week8_bench_medium;"
    elif request_type == "insert_medium":
        rid = 1000000 + idx
        sql = f"INSERT INTO week8_bench_medium VALUES ({rid},'u{rid}','u{rid}@x.com');"
    else:
        sql = "SELECT * FROM week8_bench_small;"

    body = json.dumps({"sql": sql}, separators=(",", ":"))
    return (
        "POST /query HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        f"Content-Length: {len(body.encode('utf-8'))}\r\n"
        "\r\n"
        f"{body}"
    ).encode("utf-8")


def pick_request_type(scenario: str, idx: int) -> str:
    r = (idx * 1103515245 + 12345) % 100
    if scenario == "normal":
        return "health" if r < 70 else "select_small"
    if scenario == "burst":
        return "select_small" if r < 50 else "insert_medium"
    return "insert_medium" if r < 70 else "select_medium"


def one_request(host: str, port: int, timeout_sec: float, scenario: str, idx: int):
    req_type = pick_request_type(scenario, idx)
    request = build_request(req_type, idx)
    t0 = time.perf_counter()
    status = 0
    try:
        with socket.create_connection((host, port), timeout=timeout_sec) as s:
            s.settimeout(timeout_sec)
            s.sendall(request)
            buf = b""
            while b"\r\n" not in buf:
                chunk = s.recv(64)
                if not chunk:
                    break
                buf += chunk
            line = buf.split(b"\r\n", 1)[0].split()
            if len(line) >= 2 and line[1].isdigit():
                status = int(line[1])
    except Exception:
        status = 0
    latency_ms = (time.perf_counter() - t0) * 1000.0
    return latency_ms, status


def percentile(values, p):
    if not values:
        return 0.0
    vals = sorted(values)
    pos = int((len(vals) - 1) * p)
    return float(vals[pos])


def run_case(host: str, port: int, requests: int, concurrency: int, timeout_sec: float, scenario: str):
    latencies = []
    status_counts = {}
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=concurrency) as ex:
        futures = [
            ex.submit(one_request, host, port, timeout_sec, scenario, i)
            for i in range(requests)
        ]
        for fut in as_completed(futures):
            latency_ms, status = fut.result()
            latencies.append(latency_ms)
            status_counts[status] = status_counts.get(status, 0) + 1

    elapsed_sec = max(time.perf_counter() - t0, 1e-9)
    return {
        "requests": requests,
        "concurrency": concurrency,
        "elapsed_sec": elapsed_sec,
        "throughput_rps": requests / elapsed_sec,
        "p95_ms": percentile(latencies, 0.95),
        "p99_ms": percentile(latencies, 0.99),
        "status_counts": status_counts,
        "error_503_ratio": status_counts.get(503, 0) / requests,
        "error_504_ratio": status_counts.get(504, 0) / requests,
        "success_ratio": status_counts.get(200, 0) / requests,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--scenario", choices=["normal", "burst", "saturation"], required=True)
    ap.add_argument("--requests", type=int, required=True)
    ap.add_argument("--concurrency", type=int, required=True)
    ap.add_argument("--timeout-sec", type=float, default=3.0)
    args = ap.parse_args()

    result = run_case(
        host=args.host,
        port=args.port,
        requests=args.requests,
        concurrency=args.concurrency,
        timeout_sec=args.timeout_sec,
        scenario=args.scenario,
    )
    print(json.dumps(result))


if __name__ == "__main__":
    random.seed(7)
    main()

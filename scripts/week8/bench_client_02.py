#!/usr/bin/env python3
import argparse
import json
import socket
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


REQUEST = (
    "GET /health HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Connection: close\r\n"
    "\r\n"
).encode("utf-8")


def one_request(host: str, port: int, timeout_sec: float):
    t0 = time.perf_counter()
    status = 0
    try:
        with socket.create_connection((host, port), timeout=timeout_sec) as s:
            s.settimeout(timeout_sec)
            s.sendall(REQUEST)
            buf = b""
            while b"\r\n" not in buf:
                chunk = s.recv(64)
                if not chunk:
                    break
                buf += chunk
            parts = buf.split(b"\r\n", 1)[0].split()
            if len(parts) >= 2 and parts[1].isdigit():
                status = int(parts[1])
    except Exception:
        status = 0
    latency_ms = (time.perf_counter() - t0) * 1000.0
    return latency_ms, status


def percentile(values, p):
    if not values:
        return 0.0
    sorted_vals = sorted(values)
    k = int((len(sorted_vals) - 1) * p)
    return float(sorted_vals[k])


def run_case(host: str, port: int, requests: int, concurrency: int, timeout_sec: float):
    latencies = []
    status_counts = {}
    lock = threading.Lock()
    t0 = time.perf_counter()

    with ThreadPoolExecutor(max_workers=concurrency) as ex:
        futures = [ex.submit(one_request, host, port, timeout_sec) for _ in range(requests)]
        for fut in as_completed(futures):
            latency_ms, status = fut.result()
            with lock:
                latencies.append(latency_ms)
                status_counts[status] = status_counts.get(status, 0) + 1

    elapsed_sec = max(time.perf_counter() - t0, 1e-9)
    throughput_rps = requests / elapsed_sec
    count_503 = status_counts.get(503, 0)
    return {
        "requests": requests,
        "concurrency": concurrency,
        "elapsed_sec": elapsed_sec,
        "throughput_rps": throughput_rps,
        "p95_ms": percentile(latencies, 0.95),
        "status_counts": status_counts,
        "error_503_ratio": count_503 / requests,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--requests", type=int, required=True)
    ap.add_argument("--concurrency", type=int, required=True)
    ap.add_argument("--timeout-sec", type=float, default=3.0)
    args = ap.parse_args()
    result = run_case(args.host, args.port, args.requests, args.concurrency, args.timeout_sec)
    print(json.dumps(result))


if __name__ == "__main__":
    main()

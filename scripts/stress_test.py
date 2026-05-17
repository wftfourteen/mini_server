#!/usr/bin/env python3
import argparse
import json
import os
import socket
import statistics
import threading
import time


def recv_response(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("connection closed before headers")
        data += chunk

    header_end = data.index(b"\r\n\r\n") + 4
    headers = data[:header_end].decode("iso-8859-1")
    content_length = 0
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
            break

    body = data[header_end:]
    while len(body) < content_length:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("connection closed before body")
        body += chunk

    return headers, body[:content_length]


def worker(args, worker_id, result):
    ok = 0
    failed = 0
    latencies = []
    deadline = time.time() + args.duration

    while time.time() < deadline:
        try:
            with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
                sock.settimeout(args.timeout)
                for _ in range(args.keepalive):
                    started = time.perf_counter()
                    request = (
                        f"GET {args.path} HTTP/1.1\r\n"
                        f"Host: {args.host}\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n"
                    ).encode("ascii")
                    sock.sendall(request)
                    headers, _ = recv_response(sock)
                    elapsed_ms = (time.perf_counter() - started) * 1000
                    if "200 OK" in headers:
                        ok += 1
                        latencies.append(elapsed_ms)
                    else:
                        failed += 1
        except Exception:
            failed += 1

    result[worker_id] = (ok, failed, latencies)


def main():
    parser = argparse.ArgumentParser(description="MiniServer stress test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--path", default="/")
    parser.add_argument("--clients", type=int, default=64)
    parser.add_argument("--duration", type=int, default=10)
    parser.add_argument("--keepalive", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--report")
    args = parser.parse_args()

    results = {}
    threads = []
    started = time.perf_counter()

    for worker_id in range(args.clients):
        thread = threading.Thread(target=worker, args=(args, worker_id, results))
        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join()

    elapsed = time.perf_counter() - started
    total_ok = sum(value[0] for value in results.values())
    total_failed = sum(value[1] for value in results.values())
    latencies = [latency for value in results.values() for latency in value[2]]

    print("MiniServer stress result")
    print(f"  target: {args.host}:{args.port}{args.path}")
    print(f"  clients: {args.clients}")
    print(f"  duration: {elapsed:.2f}s")
    print(f"  ok: {total_ok}")
    print(f"  failed: {total_failed}")
    print(f"  qps: {total_ok / elapsed:.2f}")

    summary = {
        "target": f"{args.host}:{args.port}{args.path}",
        "clients": args.clients,
        "duration_seconds": round(elapsed, 4),
        "ok": total_ok,
        "failed": total_failed,
        "qps": round(total_ok / elapsed, 4),
    }

    if latencies:
        latencies.sort()
        p50 = statistics.median(latencies)
        p95 = latencies[int(len(latencies) * 0.95) - 1]
        p99 = latencies[int(len(latencies) * 0.99) - 1]
        print(f"  latency p50: {p50:.2f} ms")
        print(f"  latency p95: {p95:.2f} ms")
        print(f"  latency p99: {p99:.2f} ms")
        summary.update(
            {
                "latency_p50_ms": round(p50, 4),
                "latency_p95_ms": round(p95, 4),
                "latency_p99_ms": round(p99, 4),
            }
        )

    if args.report:
        os.makedirs(os.path.dirname(args.report) or ".", exist_ok=True)
        with open(args.report, "w", encoding="utf-8") as handle:
            json.dump(summary, handle, ensure_ascii=True, indent=2)
            handle.write("\n")

    return 0 if total_ok > 0 and total_failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

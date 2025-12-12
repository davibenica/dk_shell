#!/usr/bin/env python3

import argparse
import random
import socket
import strings
import threading
import time
from typing import Dict, Tuple


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall((line + "\n").encode("utf-8"))


def read_line(sock: socket.socket) -> str:
    buf = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            break
        if ch == b"\n":
            break
        if ch != b"\r":
            buf.extend(ch)
    return buf.decode("utf-8")


def send_all(sock: socket.socket, data: bytes) -> None:
    sock.sendall(data)


def recv_all(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("Connection closed while receiving data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


class Metrics:
    def __init__(self):
        self.lock = threading.Lock()
        self.stats: Dict[str, Dict[str, float]] = {
            name: {"count": 0, "total": 0.0, "min": float("inf"), "max": 0.0}
            for name in ("upload", "download", "list", "delete")
        }
        self.bytes_up = 0
        self.bytes_down = 0
        self.cycles = 0
        self.errors = []

    def record_op(self, op: str, duration: float, bytes_up: int = 0, bytes_down: int = 0) -> None:
        with self.lock:
            stat = self.stats[op]
            stat["count"] += 1
            stat["total"] += duration
            stat["min"] = min(stat["min"], duration)
            stat["max"] = max(stat["max"], duration)
            self.bytes_up += bytes_up
            self.bytes_down += bytes_down

    def record_cycle(self) -> None:
        with self.lock:
            self.cycles += 1

    def record_error(self, message: str) -> None:
        with self.lock:
            self.errors.append(message)


class Worker(threading.Thread):
    def __init__(self, idx: int, host: str, port: int, payload: bytes,
                 filename_prefix: str, end_time: float, metrics: Metrics):
        super().__init__(daemon=True)
        self.idx = idx
        self.host = host
        self.port = port
        self.payload = payload
        self.filename = f"{filename_prefix}_t{idx}"
        self.end_time = end_time
        self.metrics = metrics
        self.iterations = 0

    def _timed(self, op: str, bytes_up: int, bytes_down: int, fn) -> None:
        start = time.perf_counter()
        result = fn()
        elapsed = time.perf_counter() - start
        self.metrics.record_op(op, elapsed, bytes_up=bytes_up, bytes_down=bytes_down)
        return result

    def run(self) -> None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.connect((self.host, self.port))
        except Exception as exc:
            self.metrics.record_error(f"[thread {self.idx}] failed to connect: {exc}")
            return

        try:
            while time.perf_counter() < self.end_time:
                iteration = self.iterations
                try:
                    self.perform_cycle(sock)
                    self.metrics.record_cycle()
                    self.iterations += 1
                except Exception as exc:
                    self.metrics.record_error(
                        f"[thread {self.idx}] iteration {iteration}: {exc}"
                    )
                    break
        finally:
            sock.close()

    def perform_cycle(self, sock: socket.socket) -> None:
        payload = self.payload
        filename = f"{self.filename}.{self.iterations}"

        def do_upload():
            send_line(sock, f"UPLOAD|{filename}|{len(payload)}")
            send_all(sock, payload)
            resp = read_line(sock)
            if not resp.startswith("OK|"):
                raise RuntimeError(f"UPLOAD failed: {resp}")
        self._timed("upload", bytes_up=len(payload), bytes_down=0, fn=do_upload)

        def do_download():
            send_line(sock, f"DOWNLOAD|{filename}")
            header = read_line(sock)
            parts = header.split("|")
            if len(parts) < 3 or parts[0] != "OK" or parts[1] != "DATA":
                raise RuntimeError(f"DOWNLOAD bad header: {header}")
            size = int(parts[2])
            data = recv_all(sock, size)
            if data != payload:
                raise RuntimeError("DOWNLOAD data mismatch")
        self._timed("download", bytes_up=0, bytes_down=len(payload), fn=do_download)

        def do_list():
            send_line(sock, "LIST")
            header = read_line(sock)
            if not header.startswith("OK|"):
                raise RuntimeError(f"LIST bad header: {header}")
            found = False
            while True:
                entry = read_line(sock)
                if entry == "":
                    break
                if entry == filename:
                    found = True
            if not found:
                raise RuntimeError(f"LIST missing {filename}")
        self._timed("list", bytes_up=0, bytes_down=0, fn=do_list)

        def do_delete():
            send_line(sock, f"DELETE|{filename}")
            resp = read_line(sock)
            if resp != "OK|File deleted successfully":
                raise RuntimeError(f"DELETE failed: {resp}")
        self._timed("delete", bytes_up=0, bytes_down=0, fn=do_delete)


def make_payload(length: int, seed: int) -> bytes:
    rng = random.Random(seed)
    chars = rng.choices(string.ascii_letters, k=length)
    return "".join(chars).encode("ascii")


def format_latency(stat: Dict[str, float]) -> str:
    if stat["count"] == 0:
        return "n/a"
    avg = (stat["total"] / stat["count"]) * 1000
    min_ms = stat["min"] * 1000
    max_ms = stat["max"] * 1000
    return f"{avg:.2f} ms avg (min {min_ms:.2f}, max {max_ms:.2f})"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Concurrent performance stress test for the cloud server."
    )
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=8080, help="Server port")
    parser.add_argument("--threads", type=int, default=8, help="Concurrent worker threads")
    parser.add_argument("--duration", type=float, default=10.0,
                        help="How long to run the test (seconds)")
    parser.add_argument("--payload-len", type=int, default=4096,
                        help="Payload size used for uploads/downloads")
    parser.add_argument("--filename-prefix", default="perf_file",
                        help="Server-side filename prefix for temporary files")
    args = parser.parse_args()

    metrics = Metrics()
    payloads = [make_payload(args.payload_len, i) for i in range(args.threads)]
    end_time = time.perf_counter() + args.duration

    print(
        f"Starting performance test against {args.host}:{args.port}\n"
        f"Threads:    {args.threads}\n"
        f"Duration:   {args.duration:.1f}s\n"
        f"PayloadLen: {args.payload_len} bytes\n"
    )

    workers = [
        Worker(
            idx=i,
            host=args.host,
            port=args.port,
            payload=payloads[i],
            filename_prefix=args.filename_prefix,
            end_time=end_time,
            metrics=metrics,
        )
        for i in range(args.threads)
    ]

    start_time = time.perf_counter()
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    finish_time = time.perf_counter()

    actual_duration = finish_time - start_time

    print("\n=== Performance Summary ===")
    total_ops = sum(stat["count"] for stat in metrics.stats.values())
    ops_per_sec = total_ops / actual_duration if actual_duration > 0 else 0.0
    print(f"Total cycles completed: {metrics.cycles}")
    print(f"Total operations:       {total_ops}")
    print(f"Aggregate ops/sec:      {ops_per_sec:.2f}")
    if actual_duration > 0:
        up_mbps = (metrics.bytes_up * 8 / 1_000_000) / actual_duration
        down_mbps = (metrics.bytes_down * 8 / 1_000_000) / actual_duration
        print(f"Upload throughput:      {metrics.bytes_up} bytes "
              f"({up_mbps:.2f} Mbit/s)")
        print(f"Download throughput:    {metrics.bytes_down} bytes "
              f"({down_mbps:.2f} Mbit/s)")

    print("\nLatency by operation:")
    for name in ("upload", "download", "list", "delete"):
        stat = metrics.stats[name]
        print(f"  {name:8s}: {stat['count']} ops, {format_latency(stat)}")

    if metrics.errors:
        print("\nErrors encountered:")
        for err in metrics.errors:
            print(f"  {err}")
        exit_code = 1
    else:
        print("\nNo errors detected during the run.")
        exit_code = 0

    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()

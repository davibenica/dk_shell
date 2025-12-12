#!/usr/bin/env python3
import argparse
import socket
import threading
import time
import random
import sys

def send_line(sock: socket.socket, line: str) -> None:
    data = (line + "\n").encode("utf-8")
    sock.sendall(data)

def read_line(sock: socket.socket) -> str:
    buf = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            break
        if ch == b"\n":
            break
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


class Worker(threading.Thread):
    def __init__(self, idx, host, port, filename, iterations, payload_len,
                 all_payloads, errors, lock):
        super().__init__()
        self.idx = idx
        self.host = host
        self.port = port
        self.filename = filename
        self.iterations = iterations
        self.payload_len = payload_len
        self.all_payloads = all_payloads  
        self.errors = errors
        self.lock = lock
        self.delete_prefix = f"{self.filename}.tmp.t{self.idx}"

    def run(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((self.host, self.port))
        except Exception as e:
            with self.lock:
                self.errors.append(f"[THREAD {self.idx}] Failed to connect: {e}")
            return

        my_payload = self.all_payloads[self.idx]
        for i in range(self.iterations):
            try:
                # --- UPLOAD ---
                send_line(sock, f"UPLOAD|{self.filename}|{len(my_payload)}")
                send_all(sock, my_payload)

                resp = read_line(sock)
                if not resp.startswith("OK|"):
                    with self.lock:
                        self.errors.append(
                            f"[THREAD {self.idx}] Iter {i}: UPLOAD error resp: {resp}"
                        )
                       
                        continue

                
                time.sleep(random.uniform(0.0, 0.01))

                
                send_line(sock, f"DOWNLOAD|{self.filename}")
                resp = read_line(sock)
                
                parts = resp.split("|")
                if len(parts) < 3 or parts[0] != "OK" or parts[1] != "DATA":
                    with self.lock:
                        self.errors.append(
                            f"[THREAD {self.idx}] Iter {i}: DOWNLOAD bad header: {resp}"
                        )
                        continue

                size = int(parts[2])
                data = recv_all(sock, size)

                if data not in self.all_payloads:
                    with self.lock:
                        self.errors.append(
                            f"[THREAD {self.idx}] Iter {i}: CORRUPT DATA "
                            f"(len={len(data)}). Not equal to any known payload."
                        )
                        self.errors.append(f"    First 32 bytes: {data[:32]!r}")

                files = self.request_file_list(
                    sock, i, f"checking presence of {self.filename}"
                )
                if files is not None and self.filename not in files:
                    with self.lock:
                        self.errors.append(
                            f"[THREAD {self.idx}] Iter {i}: LIST missing "
                            f"expected file {self.filename}"
                        )

                self.exercise_delete_endpoint(sock, my_payload, i)

                time.sleep(random.uniform(0.0, 0.01))

            except Exception as e:
                with self.lock:
                    self.errors.append(
                        f"[THREAD {self.idx}] Iter {i}: Exception: {e}"
                    )

        sock.close()

    def request_file_list(self, sock, iteration, context):
        try:
            send_line(sock, "LIST")
            header = read_line(sock)
        except Exception as exc:
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: LIST failed during {context}: {exc}"
                )
            return None

        if not header:
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: LIST empty/EOF response during {context}"
                )
            return None

        if not header.startswith("OK|"):
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: LIST bad header "
                    f"during {context}: {header}"
                )
            return None

        files = []
        while True:
            entry = read_line(sock)
            if entry == "":
                break
            files.append(entry)
        return files

    def exercise_delete_endpoint(self, sock, payload, iteration):
        tmp_name = f"{self.delete_prefix}.{iteration}"
        send_line(sock, f"UPLOAD|{tmp_name}|{len(payload)}")
        send_all(sock, payload)
        resp = read_line(sock)
        if not resp or not resp.startswith("OK|"):
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: DELETE prep upload failed: {resp}"
                )
            return

        files = self.request_file_list(
            sock, iteration, f"verifying presence of {tmp_name}"
        )
        if files is not None and tmp_name not in files:
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: LIST missing delete target {tmp_name}"
                )

        send_line(sock, f"DELETE|{tmp_name}")
        resp = read_line(sock)
        if resp != "OK|File deleted successfully":
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: DELETE failed "
                    f"for {tmp_name}: {resp}"
                )
            return

        send_line(sock, f"DOWNLOAD|{tmp_name}")
        resp = read_line(sock)
        if not resp or not resp.startswith("ERROR|"):
            with self.lock:
                self.errors.append(
                    f"[THREAD {self.idx}] Iter {iteration}: DOWNLOAD after DELETE "
                    f"returned unexpected response: {resp}"
                )


def main():
    parser = argparse.ArgumentParser(
        description="Stress-test the cloud storage server for thread safety."
    )
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=8080, help="Server port")
    parser.add_argument("--threads", type=int, default=8,
                        help="Number of concurrent client threads")
    parser.add_argument("--iterations", type=int, default=100,
                        help="Number of upload+download cycles per thread")
    parser.add_argument("--filename", default="concurrent_test.bin",
                        help="Server-side filename to use in the test")
    parser.add_argument("--payload-len", type=int, default=4096,
                        help="Bytes per uploaded file")
    args = parser.parse_args()

    print(
        f"Starting thread-safety test against {args.host}:{args.port}\n"
        f"Threads:    {args.threads}\n"
        f"Iterations: {args.iterations} per thread\n"
        f"Filename:   {args.filename}\n"
        f"PayloadLen: {args.payload_len} bytes\n"
    )

    all_payloads = []
    for i in range(args.threads):
        ch = chr(ord('A') + (i % 26))
        pattern = (f"T{i:02d}-" + ch * 10).encode("utf-8")
        repeat_factor = (args.payload_len + len(pattern) - 1) // len(pattern)
        payload = (pattern * repeat_factor)[:args.payload_len]
        all_payloads.append(payload)

    errors = []
    lock = threading.Lock()

    workers = [
        Worker(i, args.host, args.port, args.filename, args.iterations,
               args.payload_len, all_payloads, errors, lock)
        for i in range(args.threads)
    ]

    start = time.time()
    for w in workers:
        w.start()

    for w in workers:
        w.join()
    end = time.time()

    print(f"\nTest completed in {end - start:.2f} seconds.")

    if errors:
        print("\n=== ERRORS / POSSIBLE THREAD-SAFETY ISSUES DETECTED ===")
        for e in errors:
            print(e)
        print(f"\nTotal errors: {len(errors)}")
        sys.exit(1)
    else:
        print("\nNo data corruption detected in this run.")

if __name__ == "__main__":
    main()

# Custom Cloud-Enabled Shell

This project implements a Unix-style command shell that was built entirely from scratch. In addition to running local programs and handling pipelines/semicolons, the shell understands a small suite of built-in commands that talk to an accompanying cloud storage server. You can connect to the server, upload/download files, list its contents, and delete remote files without leaving the shell.

## Project Layout

- `src/` – core shell sources (`shell.cpp`, `process.cpp`, `tsh.cpp`, etc.).
- `include/` – headers shared between the shell and the server (protocol definitions, process/shell interfaces).
- `cloud_server` – C++ server binary that exposes `UPLOAD`, `DOWNLOAD`, `LIST`, and `DELETE`.
- `test/` – GoogleTest unit tests plus Python stress/performance harnesses.
- `Makefile` – builds the shell (`dk_shell_app`), unit tests (`dk_shell_test`), and server (`cloud_server`).

## Building

```bash
make            # builds the shell, tests, and server
```

Artifacts:

- `./dk_shell_app` – interactive shell
- `./dk_shell_test` – GoogleTest suite
- `./cloud_server` – storage server

## Running the Shell

1. Start the cloud server (default port 8080):
   ```bash
   ./cloud_server
   ```
2. Launch the shell:
   ```bash
   ./dk_shell_app
   ```
3. Use standard Unix commands (e.g., `ls`, `grep`, pipelines) or the built-in cloud commands:
   - `ccon <host> <port>` – connect to the server.
   - `cdisc` – disconnect.
   - `cput <local> <remote>` – upload a file.
   - `cget <remote> <local>` – download.
   - `crm <remote>` – delete.
   - `cls` – list files stored on the server.

## Testing & Verification

- **Unit tests** – exercise parsing, piping, built-in detection, and basic shell behavior:
  ```bash
  ./dk_shell_test
  ```
- **Thread safety stress test** (`test/thread_safety_test.py`) – runs concurrent upload/download/delete loops to catch race conditions. Requires a running `cloud_server`.
- **Performance stress test** (`test/performance_test.py`) – measures throughput/latency for the server under configurable load and reports detailed metrics.

Example performance run:

```bash
./cloud_server 9090 &
python3 test/performance_test.py --port 9090 --threads 8 --duration 15
```

## Notes

- The shell is intentionally minimalist: job control and advanced POSIX features are out of scope.
- All networking uses the simple line-based protocol defined in `include/protocol.h`.
- Stress scripts assume the server stores files under `./server_files` relative to the repo root.

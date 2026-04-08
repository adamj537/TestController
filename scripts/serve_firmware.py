#!/usr/bin/env python3
"""serve_firmware.py — Serve the built firmware.bin over HTTP for OTA.

Starts a simple HTTP server in the PlatformIO build output directory so the
TC can download it via 'ota update http://<host>:8080/firmware.bin'.

Usage:
    python3 scripts/serve_firmware.py              # serves on port 8080
    python3 scripts/serve_firmware.py --port 9090
    python3 scripts/serve_firmware.py --env esp32-Devkit
    python3 scripts/serve_firmware.py --once        # exit after first GET

Prints the OTA command to paste into the TC console.
"""

import argparse
import http.server
import os
import socket
import subprocess
import sys


def wsl_ip() -> str:
    try:
        out = subprocess.check_output(["hostname", "-I"], text=True).split()
        return out[0] if out else "127.0.0.1"
    except Exception:
        return "127.0.0.1"


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--env",  default="esp32-Devkit", help="PlatformIO env name")
    p.add_argument("--once", action="store_true",    help="Exit after first GET")
    args = p.parse_args()

    # Resolve the build directory relative to this script's parent
    script_dir  = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    build_dir   = os.path.join(project_dir, ".pio", "build", args.env)
    firmware    = os.path.join(build_dir, "firmware.bin")

    if not os.path.exists(firmware):
        print(f"ERROR: firmware not found at {firmware}", file=sys.stderr)
        print(f"       Run 'pio run -e {args.env}' first.", file=sys.stderr)
        sys.exit(1)

    host = wsl_ip()
    ota_url = f"http://{host}:{args.port}/firmware.bin"

    print(f"Serving {build_dir}/ on port {args.port} ...")
    print(f"OTA command: ota update {ota_url}")
    print("Press Ctrl-C to stop.\n")
    os.chdir(build_dir)

    requests_served = [0]

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt: str, *a: object) -> None:
            super().log_message(fmt, *a)
            requests_served[0] += 1
            if args.once and requests_served[0] >= 1:
                # Signal main loop to exit after this request completes
                pass

        def do_GET(self) -> None:
            super().do_GET()
            if args.once and requests_served[0] >= 1:
                raise SystemExit(0)

    print("Server started")
    with http.server.HTTPServer(("", args.port), Handler) as srv:
        try:
            srv.serve_forever()
        except (KeyboardInterrupt, SystemExit):
            pass

    print("\nServer stopped.")


if __name__ == "__main__":
    main()

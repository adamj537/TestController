#!/usr/bin/env python3
"""diag_pfw_version.py — Check PFW version sources on the TC.

Tests all three version resolution paths:
  1. Live cache (selftest_get_pfw_version) — requires run_dut_version
  2. Stored partition (dut_fw header) — via swd fw version cmd if available
  3. NVS (firmware/dut_pfw_ver)

Also checks NBIRTH and versions_get DCMD output by running selftest
dut_version to populate the cache and checking MQTT log.

Usage:
    python3 scripts/diag_pfw_version.py [--host 192.168.50.21]
"""

import argparse
import socket
import sys
import time

from tc_config import HOST, PORT


class TC:
    def __init__(self, host: str = HOST):
        self.host = host
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(10)
        self.sock.connect((self.host, PORT))
        time.sleep(0.3)
        self._drain()

    def _drain(self) -> None:
        self.sock.settimeout(0.3)
        try:
            while self.sock.recv(4096):
                pass
        except socket.timeout:
            pass
        self.sock.settimeout(10)

    def cmd(self, command: str, wait: float = 5.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + wait
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if "g3-tc|" in chunk and ">" in chunk:
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def cmd_long(self, command: str, stop_marker: str = "Results:",
                 timeout: float = 12.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + timeout
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk:
                        time.sleep(0.3)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except socket.timeout:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


def section(title: str) -> None:
    print(f"\n--- {title} ---")


def main() -> int:
    parser = argparse.ArgumentParser(description="PFW version diagnostic")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    tc = TC(args.host)
    print(f"Connecting to {args.host}:{PORT}...")
    try:
        tc.connect()
    except Exception as e:
        print(f"FAIL: {e}")
        return 1

    section("TC firmware version")
    print(tc.cmd("version").strip())

    section("NVS: firmware/dut_pfw_ver")
    print(tc.cmd("nvs get firmware dut_pfw_ver", wait=3).strip())

    section("NVS: firmware/product_fw_ver")
    print(tc.cmd("nvs get firmware product_fw_ver", wait=3).strip())

    section("MQTT log before dut_version (check for versions DDATA)")
    print(tc.cmd("mqtt log 5", wait=3).strip())

    section("Power DUT and run selftest dut_version")
    tc.cmd("dut pause", wait=2)
    tc.cmd("vdac_voltage 1 3300", wait=5)
    tc.cmd("mux select 0 0", wait=2)
    time.sleep(3)
    resp = tc.cmd_long("selftest dut_version", stop_marker="Results:", timeout=12)
    print(resp.strip())

    section("MQTT log after dut_version")
    print(tc.cmd("mqtt log 5", wait=3).strip())

    section("Teardown")
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=3)
    tc.cmd("dut resume", wait=2)
    print("  Done")

    tc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

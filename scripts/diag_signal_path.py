#!/usr/bin/env python3
"""diag_signal_path.py — Isolate PA9 signal path failure.

The DUT MCU is running (UART works, PA9 ODR=HIGH via SWD), but TIE MUX2 ch2
reads only 36 mV. Something in the PA9 → TIE MUX2 ch2 → ADC128 CH2 path is
broken. This script probes the signal chain to find where.

Checks:
  1. TIE MUX2 all channels (see if other MUX2 inputs work)
  2. TIE MUX0 and MUX1 spot checks (verify MUX ICs work)
  3. DUT PERIPHERAL_ADC_SCAN (DUT's own view of its pins)
  4. DUT PA9 state via UART command (if available)
  5. Raw ADC128 channel scan (all channels without MUX selection)

Usage:
    python3 scripts/diag_signal_path.py [--host 192.168.50.21]
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

    def cmd(self, command: str, wait: float = 3.0) -> str:
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

    def cmd_long(self, command: str, stop_marker: str = "g3-tc|",
                 timeout: float = 10.0) -> str:
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


def section(n: int, title: str) -> None:
    print(f"\n{'='*60}")
    print(f"[{n}] {title}")
    print(f"{'='*60}")


def main() -> int:
    parser = argparse.ArgumentParser(description="PA9 signal path diagnostic")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    tc = TC(args.host)

    print(f"Connecting to {args.host}:{PORT}...")
    try:
        tc.connect()
        print("  OK")
    except Exception as e:
        print(f"  FAIL: {e}")
        return 1

    # Setup: pause dut_detect, power DUT, assert PB-A
    tc.cmd("dut pause", wait=2)
    tc.cmd("vdac_voltage 1 3300", wait=5)
    tc.cmd("mux select 0 0", wait=2)
    print("  VDUT1 on, PB-A asserted, dut_detect paused")
    time.sleep(2)  # Let DUT boot

    # ── 1. TIE MUX2 all channels ──────────────────────────────────────
    section(1, "TIE MUX2 (U12) all channels — PA9 expected on ch2")
    for ch in range(16):
        resp = tc.cmd(f"tie read 2 {ch}", wait=3)
        # Parse "TIE 2 N XXXX mV"
        mv = "?"
        for line in resp.splitlines():
            if "TIE" in line and "mV" in line:
                parts = line.strip().split()
                for i, p in enumerate(parts):
                    if p == "mV" and i > 0:
                        mv = parts[i - 1]
                        break
                break
        print(f"  MUX2 ch{ch:2d}: {mv:>6s} mV")

    # ── 2. TIE MUX0 and MUX1 spot checks ────────────────────────────
    section(2, "TIE MUX0 (U10) and MUX1 (U11) spot checks")
    for mux in [0, 1]:
        for ch in [0, 1, 2, 3]:
            resp = tc.cmd(f"tie read {mux} {ch}", wait=3)
            mv = "?"
            for line in resp.splitlines():
                if "TIE" in line and "mV" in line:
                    parts = line.strip().split()
                    for i, p in enumerate(parts):
                        if p == "mV" and i > 0:
                            mv = parts[i - 1]
                            break
                    break
            print(f"  MUX{mux} ch{ch}: {mv:>6s} mV")

    # ── 3. DUT PERIPHERAL_ADC_SCAN ───────────────────────────────────
    section(3, "DUT peripheral ADC scan (DUT's own pin readings)")
    resp = tc.cmd_long("dut scan", stop_marker="g3-tc|", timeout=10)
    print(resp.strip())

    # ── 4. DUT raw commands ──────────────────────────────────────────
    section(4, "DUT raw UART commands")
    # Try asking DUT about its heartbeat/PA9 state
    print("  STATUS:")
    resp = tc.cmd("dut raw STATUS", wait=5)
    print(resp.strip())

    print("\n  HEARTBEAT:")
    resp = tc.cmd("dut raw HEARTBEAT", wait=5)
    print(resp.strip())

    print("\n  GPIO_STATE:")
    resp = tc.cmd("dut raw GPIO_STATE", wait=5)
    print(resp.strip())

    # ── 5. selftest vdut (powered channel check) ────────────────────
    section(5, "selftest vdut (powered channel readings)")
    resp = tc.cmd_long("selftest vdut", stop_marker="Results:", timeout=15)
    print(resp.strip())

    # ── 6. Check the analog switch state ─────────────────────────────
    section(6, "U8 mux state and I2C bus scan")
    print("  U8 mux status:")
    resp = tc.cmd("mux status", wait=3)
    print(resp.strip())

    print("\n  I2C device scan:")
    resp = tc.cmd("i2c scan", wait=5)
    print(resp.strip())

    # ── 7. Teardown ──────────────────────────────────────────────────
    section(7, "Teardown")
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=3)
    tc.cmd("dut resume", wait=2)
    print("  Done")

    tc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

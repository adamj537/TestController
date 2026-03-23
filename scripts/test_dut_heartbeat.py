#!/usr/bin/env python3
"""test_dut_heartbeat.py — Validate DUT heartbeat (PA9) with explicit power sequencing.

Sequence:
  1. Connect to TC via TCP console
  2. Verify DUT is present (dut sample)
  3. Enable VDUT at 3300 mV via calibrated vdac_voltage (both channels)
  4. Assert PB-A (mux select 0 0 — active low)
  5. Wait for DUT boot + PA9 toggle to stabilise (--boot-wait, default 2.0 s)
  6. Run selftest heartbeat (2.5 s sample window)
  7. Teardown: mux release, vdac off
  8. Report pass/fail

This script was written to validate the fix for the power-sequencing bug where
power_check (recipe step 7) disabled VDUT before the heartbeat step (step 8)
could run, leaving PA9 with no drive.

Usage:
    python3 scripts/test_dut_heartbeat.py [--host 10.0.0.244] [--boot-wait 2.0]

Prerequisites:
    - DUT physically seated in fixture
    - PFW (dut-firmware) already flashed to DUT  (swd flash --store, then swd flash)
    - VDUT calibration loaded on TC  (cal load, or cal vdut set slope intercept)

Exit codes:
    0 = heartbeat PASS
    1 = heartbeat FAIL or sequence error
"""

import argparse
import socket
import sys
import time

HOST = "10.0.0.244"
PORT = 4242

# Minimum time after VDUT+PB-A enable for DUT firmware to boot and start
# toggling PA9 at 1 Hz.  selftest heartbeat needs at least one full toggle
# cycle (1 s) within its 2.5 s window — 2 s gives one full cycle of margin.
DEFAULT_BOOT_WAIT = 2.0

# Threshold for DUT present detection (mV). DUT present ≈ 1666 mV, absent ≈ 2560 mV.
DUT_DETECT_THRESHOLD_MV = 2000


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

    def cmd_long(self, command: str, stop_marker: str = "Results:", timeout: float = 10.0) -> str:
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


class Results:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, name: str, condition: bool, detail: str = "") -> bool:
        if condition:
            self.passed += 1
            mark = "\033[32mPASS\033[0m"
        else:
            self.failed += 1
            mark = "\033[31mFAIL\033[0m"
        line = f"  [{mark}] {name}"
        if detail:
            line += f"  ({detail})"
        print(line)
        return condition

    def summary(self) -> bool:
        total = self.passed + self.failed
        print(f"\nDUT heartbeat test: {self.passed}/{total} passed, {self.failed} failed")
        return self.failed == 0


def teardown(tc: TC) -> None:
    """Always release PB-A and disable VDUT regardless of test outcome."""
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=2)
    print("  [teardown] PB-A released, VDUT disabled")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Test DUT heartbeat (PA9) with proper power sequencing",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default=HOST, help=f"TC IP address (default: {HOST})")
    parser.add_argument(
        "--boot-wait", type=float, default=DEFAULT_BOOT_WAIT,
        help=f"Seconds to wait after VDUT enable for DUT to boot (default: {DEFAULT_BOOT_WAIT})",
    )
    args = parser.parse_args()

    r = Results()
    tc = TC(args.host)

    # ── 1. Connect ───────────────────────────────────────────────────────────
    print(f"[1] Connect to {args.host}:{PORT}")
    try:
        tc.connect()
        r.check("TCP connect", True, f"{args.host}:{PORT}")
    except Exception as e:
        r.check("TCP connect", False, str(e))
        sys.exit(1)

    # ── 2. Verify DUT is present ─────────────────────────────────────────────
    print("\n[2] DUT presence check")
    resp = tc.cmd("dut sample", wait=5)
    present = False
    adc_mv = -1
    for line in resp.splitlines():
        if "mV" in line and "DUT sample:" in line:
            try:
                # "DUT sample: 1666 mV  → PRESENT  (threshold 2000 mV)"
                adc_mv = int(line.split(":")[1].strip().split()[0])
                present = adc_mv < DUT_DETECT_THRESHOLD_MV
            except (IndexError, ValueError):
                pass
            break

    if not r.check("DUT present", present,
                   f"ADC={adc_mv} mV  threshold={DUT_DETECT_THRESHOLD_MV} mV"):
        print("\n  Ensure DUT is seated before running this test.")
        tc.close()
        sys.exit(1)

    # ── 3. Enable VDUT at 3300 mV ────────────────────────────────────────────
    print("\n[3] Enable VDUT at 3300 mV")
    resp = tc.cmd("vdac_voltage both 3300", wait=3)
    vdut_ok = "VDUT1: 3300 mV" in resp or "3300" in resp
    if not r.check("VDUT enabled at 3300 mV", vdut_ok, resp.strip().replace("\n", " ")[:80]):
        # Calibration might not be set; print guidance
        if "calibration not set" in resp.lower() or "not calibrated" in resp.lower():
            print("\n  VDUT calibration not set on TC.")
            print("  Run:  cal load     (restores saved calibration)")
            print("  Or:   cal vdut 0 -87 10811   cal vdut 1 -86 10832   cal save")
        tc.close()
        sys.exit(1)

    # ── 4. Assert PB-A ───────────────────────────────────────────────────────
    print("\n[4] Assert PB-A (mux select 0 0)")
    resp = tc.cmd("mux select 0 0", wait=2)
    pba_ok = "ch=0" in resp and "SIG=0" in resp
    if not r.check("PB-A asserted", pba_ok, resp.strip()):
        teardown(tc)
        tc.close()
        sys.exit(1)

    # ── 5. Wait for DUT boot ─────────────────────────────────────────────────
    print(f"\n[5] Waiting {args.boot_wait:.1f} s for DUT firmware to boot and start PA9 toggle...")
    time.sleep(args.boot_wait)

    # ── 6. Run selftest heartbeat ─────────────────────────────────────────────
    # selftest heartbeat runs for 2.5 s (25 × 100 ms samples), then prints Results:
    print("\n[6] selftest heartbeat  (2.5 s sample window)")
    resp = tc.cmd_long("selftest heartbeat", stop_marker="Results:", timeout=8.0)
    print(resp.strip())

    hb_pass = "[PASS]" in resp and "heartbeat" in resp.lower()
    hb_fail = "[FAIL]" in resp and "heartbeat" in resp.lower()

    # Extract min/max mV from output for diagnostic
    detail = ""
    for line in resp.splitlines():
        if "heartbeat" in line.lower() and ("min=" in line or "PASS" in line or "FAIL" in line):
            detail = line.strip()
            break

    r.check("DUT heartbeat (PA9 toggling)", hb_pass and not hb_fail, detail)

    # ── 7. Teardown ──────────────────────────────────────────────────────────
    print("\n[7] Teardown")
    teardown(tc)
    tc.close()

    # ── 8. Summary ───────────────────────────────────────────────────────────
    ok = r.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

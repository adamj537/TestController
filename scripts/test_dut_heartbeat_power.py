#!/usr/bin/env python3
"""test_dut_heartbeat_power.py — Heartbeat + INA219 power validation.

Extends test_dut_heartbeat.py with INA219 bus voltage, shunt voltage, and
current readings at two points: baseline (VDUT on, PB-A not yet asserted)
and post-PBA (DUT booted, PB-A held).

Sequence:
  1. Connect to TC via TCP console
  2. Verify DUT is present (dut sample)
  3. Enable VDUT at 3300 mV
  3b. INA219 baseline read (expect ~3.3 V bus, ~0 mA)
  4. Assert PB-A (mux select 0 0 — active low)
  5. Wait for DUT boot
  5b. INA219 post-PBA read (expect ~3.3 V bus, >15 mA)
  6. [--keepalive] Release PB-A, INA219 read again
  7. Run selftest heartbeat (2.5 s sample window)
  8. Teardown: mux release, vdac off

Usage:
    python3 scripts/test_dut_heartbeat_power.py
    python3 scripts/test_dut_heartbeat_power.py --keepalive
"""

import argparse
import socket
import sys
import time

from tc_config import HOST, PORT

DEFAULT_BOOT_WAIT = 2.0
DUT_DETECT_THRESHOLD_MV = 2000

INA219_ADDR = 0x40
INA219_CURRENT_LSB_UA = 10


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

    def summary(self, label: str = "DUT heartbeat+power test") -> bool:
        total = self.passed + self.failed
        print(f"\n{label}: {self.passed}/{total} passed, {self.failed} failed")
        return self.failed == 0


def _parse_i2c_read(resp: str) -> int:
    """Return raw 16-bit value from 'i2c read' response, or -1 on failure."""
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def _signed16(raw: int) -> int:
    return raw - 0x10000 if raw > 0x7FFF else raw


def read_ina219(tc: TC, label: str) -> tuple[int, int, int]:
    """Read INA219 bus voltage (mV), shunt voltage (uV), and current (uA).

    Returns (bus_mv, shunt_uv, current_ua).
    """
    addr = INA219_ADDR
    # Configure INA219
    tc.cmd(f"i2c write 0x{addr:02x} 0x00 0x21 0x9F", wait=0.5)
    tc.cmd(f"i2c write 0x{addr:02x} 0x05 0xA0 0x00", wait=0.5)
    time.sleep(0.5)

    bus_raw = _parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x02 2", wait=1))
    shunt_raw = _parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x01 2", wait=1))
    cur_raw = _parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x04 2", wait=1))

    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    shunt_uv = _signed16(shunt_raw) * 10 if shunt_raw >= 0 else -1
    current_ua = _signed16(cur_raw) * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1

    print(f"  {label}:")
    print(f"    Bus voltage : {bus_mv} mV")
    print(f"    Shunt voltage: {shunt_uv} uV")
    print(f"    Current     : {current_ua / 1000:.1f} mA  (raw=0x{cur_raw:04X})")
    return bus_mv, shunt_uv, current_ua


def teardown(tc: TC) -> None:
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=2)
    tc.cmd("dut resume", wait=2)
    print("  [teardown] PB-A released, VDUT disabled, auto-start resumed")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Test DUT heartbeat + INA219 power readings",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default=HOST, help=f"TC IP address (default: {HOST})")
    parser.add_argument(
        "--boot-wait", type=float, default=DEFAULT_BOOT_WAIT,
        help=f"Seconds to wait after VDUT enable for DUT to boot (default: {DEFAULT_BOOT_WAIT})",
    )
    parser.add_argument(
        "--keepalive", action="store_true",
        help="Release PB-A after boot-wait, then verify DUT holds power via KEEPALIVE latch",
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

    # ── 1b. Pause DUT auto-start ─────────────────────────────────────────────
    print("\n[1b] Pause DUT auto-start")
    tc.cmd("dut pause", wait=2)

    # ── 2. Verify DUT is present ─────────────────────────────────────────────
    print("\n[2] DUT presence check")
    resp = tc.cmd("dut sample", wait=5)
    present = False
    adc_mv = -1
    for line in resp.splitlines():
        if "mV" in line and "DUT sample:" in line:
            try:
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

    # ── 3. Enable VDUT1 at 3300 mV ──────────────────────────────────────────
    print("\n[3] Enable VDUT1 at 3300 mV")
    resp = tc.cmd("vdac_voltage 1 3300", wait=5)
    vdut_ok = "VDUT1: 3300 mV" in resp or "3300" in resp
    if not r.check("VDUT enabled at 3300 mV", vdut_ok, resp.strip().replace("\n", " ")[:80]):
        if "calibration not set" in resp.lower() or "not calibrated" in resp.lower():
            print("\n  VDUT calibration not set on TC.")
            print("  Run:  cal load     (restores saved calibration)")
        tc.close()
        sys.exit(1)

    # ── 3b. INA219 baseline read (VDUT on, PB-A not asserted) ────────────────
    print("\n[3b] INA219 baseline (VDUT on, PB-A NOT asserted)")
    bus_pre, shunt_pre, cur_pre = read_ina219(tc, "VDUT1 baseline")
    r.check("Baseline current < 5 mA", abs(cur_pre) < 5000,
            f"{cur_pre / 1000:.1f} mA")

    # ── 4. Assert PB-A ───────────────────────────────────────────────────────
    print("\n[4] Assert PB-A (mux select 0 0)")
    resp = tc.cmd("mux select 0 0", wait=2)
    pba_ok = "ch=0" in resp and "SIG=0" in resp
    if not r.check("PB-A asserted", pba_ok, resp.strip()):
        teardown(tc)
        tc.close()
        sys.exit(1)

    # ── 5. Wait for DUT boot ─────────────────────────────────────────────────
    print(f"\n[5] Waiting {args.boot_wait:.1f} s for DUT firmware to boot...")
    time.sleep(args.boot_wait)

    # ── 5b. INA219 post-PBA read (DUT booted, drawing current) ───────────────
    print("\n[5b] INA219 post-PBA (DUT booted)")
    bus_post, shunt_post, cur_post = read_ina219(tc, "VDUT1 post-PBA")
    r.check("Post-PBA current > 10 mA", cur_post > 10000,
            f"{cur_post / 1000:.1f} mA")
    r.check("Post-PBA bus voltage > 2500 mV", bus_post > 2500,
            f"{bus_post} mV")

    # ── 6. [optional] Release PB-A — DUT KEEPALIVE must take over ────────────
    if args.keepalive:
        print("\n[6] Release PB-A (mux release) — DUT KEEPALIVE must hold power")
        resp = tc.cmd("mux release", wait=2)
        released = "released" in resp.lower()
        if not r.check("PB-A released", released,
                       resp.strip().splitlines()[0] if resp.strip() else ""):
            teardown(tc)
            tc.close()
            sys.exit(1)
        time.sleep(0.5)

        print("\n[6b] INA219 post-KEEPALIVE (PB-A released, DUT self-latched)")
        bus_ka, shunt_ka, cur_ka = read_ina219(tc, "VDUT1 keepalive")
        r.check("Keepalive current > 10 mA", cur_ka > 10000,
                f"{cur_ka / 1000:.1f} mA")

    # ── 7. Run selftest heartbeat ────────────────────────────────────────────
    step = 7 if args.keepalive else 6
    print(f"\n[{step}] selftest heartbeat  (2.5 s sample window)")
    resp = tc.cmd_long("selftest heartbeat", stop_marker="Results:", timeout=8.0)
    print(resp.strip())

    hb_pass = "[PASS]" in resp and "heartbeat" in resp.lower()
    hb_fail = "[FAIL]" in resp and "heartbeat" in resp.lower()

    detail = ""
    for line in resp.splitlines():
        if "heartbeat" in line.lower() and ("min=" in line or "PASS" in line or "FAIL" in line):
            detail = line.strip()
            break

    r.check("DUT heartbeat (PA9 toggling)", hb_pass and not hb_fail, detail)

    # ── Teardown ─────────────────────────────────────────────────────────────
    td_step = step + 1
    print(f"\n[{td_step}] Teardown")
    teardown(tc)
    tc.close()

    # ── Summary ──────────────────────────────────────────────────────────────
    label = "DUT keepalive+heartbeat+power test" if args.keepalive else "DUT heartbeat+power test"
    ok = r.summary(label)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

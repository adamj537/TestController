#!/usr/bin/env python3
"""debug_cn8_5.py — Manual probe helper for CN8-5 (SCL_Tx) analog switch.

Brings the DUT up to test mode, drives PA0 HIGH (UART path, PD8=LOW),
then pauses for electrical probing before continuing.

Steps:
  1. Power VDUT1 at 3300 mV, assert PB-A
  2. Flash PFW via SWD (skip with --no-flash)
  3. Release PB-A, wait for heartbeat, ENTER_TEST
  4. Confirm PD8=LOW (INPUT_PULLDOWN — UART path active)
  5. GPIO_SET PA0 HIGH  ← pause here for probing
  6. [Press ENTER]
  7. GPIO_CLEAR PA0     ← pause here for probing
  8. [Press ENTER]
  9. Teardown

Usage:
    python3 scripts/debug_cn8_5.py
    python3 scripts/debug_cn8_5.py --no-flash
"""
from __future__ import annotations
import argparse
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (tc_cmd, tc_cmd_long, dut_cmd,            # noqa: E402
                           run_mux_read, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

VDUT_MV      = 3300
HB_TIMEOUT_S = 10.0


def bring_up(tc: TcConsole, do_flash: bool) -> bool:
    print("\n── Bring-up ─────────────────────────────────────────────────────────────")

    tc_cmd(tc, "dut pause", wait=2.0)

    print(f"  vdac_voltage 1 {VDUT_MV} ...")
    resp = tc_cmd(tc, f"vdac_voltage 1 {VDUT_MV}", wait=5.0)
    if str(VDUT_MV) not in resp:
        print(f"  ERROR: VDUT1 not confirmed: {resp.strip()[:80]}")
        return False
    print(f"  VDUT1 OK")

    print("  mux select 0 0 (assert PB-A) ...")
    tc_cmd(tc, "mux select 0 0", wait=2.0)

    if do_flash:
        print("  swd probe ...")
        resp = tc_cmd(tc, "swd probe", wait=10.0)
        if "PASS" not in resp.upper():
            print(f"  ERROR: SWD probe failed: {resp.strip()[-80:]}")
            return False

        print("  swd flash local --target pfw nopwrcycle ...")
        raw = tc_cmd_long(tc, "swd flash local --target pfw nopwrcycle",
                          stop="[PASS] swd flash", timeout=300.0)
        if "[PASS] swd flash" not in raw:
            print("  ERROR: SWD flash failed")
            return False
        print("  Flash OK")
    else:
        print("  Skipping SWD flash (--no-flash)")

    print("  mux release (PB-A → DUT KEEPALIVE) ...")
    tc_cmd(tc, "mux release", wait=2.0)

    print("  selftest heartbeat ...")
    resp = tc_cmd_long(tc, "selftest heartbeat", stop="Results:", timeout=HB_TIMEOUT_S)
    if "[PASS]" not in resp:
        print("  ERROR: heartbeat failed — DUT not alive")
        return False
    print("  Heartbeat OK")

    resp = dut_cmd(tc, "ENTER_TEST", wait=1.5)
    if "TEST_MODE" not in resp and "ALREADY" not in resp:
        print(f"  ERROR: ENTER_TEST failed: {resp}")
        return False
    print(f"  ENTER_TEST OK: {resp}")
    return True


def probe_pa0(tc: TcConsole) -> None:
    print("\n── PA0 probe session (PD8=LOW — UART path active) ───────────────────────")
    print("  PD8 is INPUT_PULLDOWN from boot → UART path (PA0→CN8-5) selected")
    print()

    # ── PA0 HIGH ──────────────────────────────────────────────────────────────
    resp = dut_cmd(tc, "GPIO_SET PA0 HIGH", wait=1.0)
    print(f"  GPIO_SET PA0 HIGH → DUT: {resp}")

    # Quick TIE mux read before pause
    mv = run_mux_read(tc, 1, 13, label="PA0 HIGH before probe")
    print(f"  TIE mux MUX1 ch13 = {mv} mV  (expect >2700 mV)")
    print()
    print("  *** PA0 is now HIGH ***")
    print("  Probe points:")
    print("    CN8-5 pogo tip           → should be ~3.3 V")
    print("    U13 pin 1 (UART IN / PA0 net) → should be ~3.3 V")
    print("    U13 pin 3 (OUT / CN8-5 net)   → should be ~3.3 V")
    print("    U13 pin 6 (IN_B / PD8 net)    → should be ~0 V  (PD8=LOW)")
    print()
    input("  Press ENTER when done probing PA0 HIGH ...")

    # ── PA0 LOW ───────────────────────────────────────────────────────────────
    resp = dut_cmd(tc, "GPIO_CLEAR PA0", wait=1.0)
    print(f"\n  GPIO_CLEAR PA0 → DUT: {resp}")

    mv = run_mux_read(tc, 1, 13, label="PA0 LOW before probe")
    print(f"  TIE mux MUX1 ch13 = {mv} mV  (expect <300 mV)")
    print()
    print("  *** PA0 is now LOW ***")
    print("  Probe points:")
    print("    CN8-5 pogo tip           → should be ~0 V")
    print("    U13 pin 1 (UART IN / PA0 net) → should be ~0 V")
    print("    U13 pin 3 (OUT / CN8-5 net)   → should be ~0 V")
    print()
    input("  Press ENTER when done probing PA0 LOW ...")


def main() -> None:
    ap = argparse.ArgumentParser(description="CN8-5 manual probe helper")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    with TcConsole() as tc:
        try:
            ok = bring_up(tc, do_flash=not args.no_flash)
            if ok:
                probe_pa0(tc)
        finally:
            teardown(tc)

    print("\nDone.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""test_cn8_5.py — CN8-5 (SCL_Tx) analog switch continuity test.

CN8-5 connects through U13 (TS5A23157) to two sources:
  PD8 LOW  → UART path: PA0  → CN8-5 (SCL_Tx)
  PD8 HIGH → I2C path:  PB10 → CN8-5 (SCL_Tx)

Each path is driven HIGH then LOW and read via TIE mux (1,13).

Usage:
    python3 scripts/test_cn8_5.py
    python3 scripts/test_cn8_5.py --no-flash
"""
from __future__ import annotations
import argparse
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, tc_cmd, tc_cmd_long, dut_cmd,   # noqa: E402
                           run_mux_read,
                           HIGH_MV_MIN, LOW_MV_MAX)

VDUT_MV      = 3300
HB_TIMEOUT_S = 10.0

CN8_5 = (1, 13)   # SCL_Tx — TIE mux key


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def check_high(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv > HIGH_MV_MIN, mv_str(mv))


def check_low(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv < LOW_MV_MAX, mv_str(mv))


# ── Phase 1: DUT bring-up (aligned to test_cn8.py) ───────────────────────────

def phase_power_and_flash(tc: TcConsole, r: Results, do_flash: bool) -> bool:
    print("\n── Phase 1: DUT bring-up ────────────────────────────────────────────────")

    print("  Stopping DUT detect task ...")
    tc_cmd(tc, "dut stop", wait=2.0)

    print(f"  Enabling VDUT1 at {VDUT_MV} mV ...")
    resp = tc_cmd(tc, f"vdac_voltage 1 {VDUT_MV}", wait=5.0)
    vdut_ok = str(VDUT_MV) in resp
    if not r.check(f"VDUT1 enabled at {VDUT_MV} mV", vdut_ok,
                   resp.strip().replace("\n", " ")[:80]):
        return False

    print("  Asserting PB-A (mux select 0 0) ...")
    resp = tc_cmd(tc, "mux select 0 0", wait=2.0)
    pba_ok = "SIG=0" in resp or "ch=0" in resp
    if not r.check("PB-A asserted", pba_ok,
                   resp.strip().splitlines()[0] if resp.strip() else ""):
        return False

    if do_flash:
        print("  SWD probe ...")
        resp = tc_cmd(tc, "swd probe", wait=10.0)
        if not r.check("SWD probe", "PASS" in resp.upper(),
                       resp.strip().splitlines()[-1] if resp.strip() else ""):
            return False

        print("  Flashing PFW (swd flash local --target pfw nopwrcycle) ...")
        raw = tc_cmd_long(tc, "swd flash local --target pfw nopwrcycle",
                          stop="[PASS] swd flash", timeout=300.0)
        if not r.check("SWD flash PFW", "[PASS] swd flash" in raw):
            return False
    else:
        print("  Skipping SWD flash (--no-flash)")

    print("  Releasing PB-A (DUT KEEPALIVE takes over) ...")
    tc_cmd(tc, "mux release", wait=2.0)
    r.check("PB-A released", True)

    return True


# ── Phases 2/3: analog switch continuity ─────────────────────────────────────

def phase_uart(tc: TcConsole, r: Results) -> bool:
    """PD8=LOW — UART path: PA0 → CN8-5."""
    # Heartbeat check first (same pattern as test_cn8.py phase_gpio_continuity)
    print("\n  selftest heartbeat ...")
    resp = tc_cmd_long(tc, "selftest heartbeat", stop="Results:",
                       timeout=HB_TIMEOUT_S)
    hb_ok = "[PASS]" in resp
    if not r.check("DUT heartbeat (pfw running)", hb_ok):
        print("  Aborting — DUT not alive")
        return False

    resp = dut_cmd(tc, "ENTER_TEST", wait=1.5)
    in_test = "TEST_MODE" in resp or "ALREADY" in resp
    if not r.check("DUT ENTER_TEST", in_test, resp):
        return False

    print("\n── UART path (PD8=LOW): PA0 → CN8-5 ────────────────────────────────────")
    print("  (PD8 INPUT PULLDOWN from boot)")

    resp = dut_cmd(tc, "GPIO_SET PA0 HIGH", wait=1.0)
    print(f"  GPIO_SET PA0 HIGH  → DUT: {resp}")
    check_high(r, "CN8-5 HIGH via PA0 (UART)",
               run_mux_read(tc, *CN8_5, label="PA0 HIGH, PD8=L"))

    resp = dut_cmd(tc, "GPIO_CLEAR PA0", wait=1.0)
    print(f"  GPIO_CLEAR PA0     → DUT: {resp}")
    check_low(r, "CN8-5 LOW  via PA0 (UART)",
              run_mux_read(tc, *CN8_5, label="PA0 LOW, PD8=L"))

    return True


def phase_i2c(tc: TcConsole, r: Results) -> None:
    """PD8=HIGH — I2C path: PB10 → CN8-5."""
    print("\n── I2C path (PD8=HIGH): PB10 → CN8-5 ───────────────────────────────────")

    resp = dut_cmd(tc, "GPIO_SET PD8 HIGH", wait=1.0)
    print(f"  GPIO_SET PD8 HIGH  → DUT: {resp}")
    time.sleep(0.3)  # switch settle

    resp = dut_cmd(tc, "GPIO_SET PB10 HIGH", wait=1.0)
    print(f"  GPIO_SET PB10 HIGH → DUT: {resp}")
    check_high(r, "CN8-5 HIGH via PB10 (I2C)",
               run_mux_read(tc, *CN8_5, label="PB10 HIGH, PD8=H"))

    resp = dut_cmd(tc, "GPIO_CLEAR PB10", wait=1.0)
    print(f"  GPIO_CLEAR PB10    → DUT: {resp}")
    check_low(r, "CN8-5 LOW  via PB10 (I2C)",
              run_mux_read(tc, *CN8_5, label="PB10 LOW, PD8=H"))

    dut_cmd(tc, "GPIO_CLEAR PD8", wait=0.5)


# ── Teardown ──────────────────────────────────────────────────────────────────

def teardown(tc: TcConsole) -> None:
    print("\n── Teardown ─────────────────────────────────────────────────────────────")
    tc_cmd(tc, "mux release", wait=2.0)
    tc_cmd(tc, "vdac off",    wait=2.0)
    tc_cmd(tc, "dut start",   wait=2.0)
    print("  PB-A released, VDUT off, DUT detect restarted")


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description="CN8-5 SCL_Tx analog switch continuity test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_power_and_flash(tc, r, do_flash=not args.no_flash)
            if ok:
                ok = phase_uart(tc, r)
            if ok:
                phase_i2c(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

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
from test_helpers import (Results, dut_cmd,                        # noqa: E402
                           run_mux_read, phase_bring_up, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

CN8_5 = (1, 13)   # SCL_Tx — TIE mux key


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def check_high(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv > HIGH_MV_MIN, mv_str(mv))


def check_low(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv < LOW_MV_MAX, mv_str(mv))


def phase_uart(tc: TcConsole, r: Results) -> None:
    """PD8=LOW — UART path: PA0 → CN8-5."""
    print("\n── UART path (PD8=LOW): PA0 → CN8-5 ────────────────────────────────────")
    print("  (PD8 INPUT PULLDOWN from boot)")

    resp = dut_cmd(tc, "GPIO_SET PA0 HIGH", wait=1.0)
    print(f"  GPIO_SET PA0 HIGH  → DUT: {resp}")
    check_high(r, "CN8-5 HIGH via PA0 (UART)", run_mux_read(tc, *CN8_5, label="PA0 HIGH, PD8=L"))

    resp = dut_cmd(tc, "GPIO_CLEAR PA0", wait=1.0)
    print(f"  GPIO_CLEAR PA0     → DUT: {resp}")
    check_low(r, "CN8-5 LOW  via PA0 (UART)", run_mux_read(tc, *CN8_5, label="PA0 LOW, PD8=L"))


def phase_i2c(tc: TcConsole, r: Results) -> None:
    """PD8=HIGH — I2C path: PB10 → CN8-5."""
    print("\n── I2C path (PD8=HIGH): PB10 → CN8-5 ───────────────────────────────────")

    resp = dut_cmd(tc, "GPIO_SET PD8 HIGH", wait=1.0)
    print(f"  GPIO_SET PD8 HIGH  → DUT: {resp}")
    time.sleep(0.3)  # switch settle

    resp = dut_cmd(tc, "GPIO_SET PB10 HIGH", wait=1.0)
    print(f"  GPIO_SET PB10 HIGH → DUT: {resp}")
    check_high(r, "CN8-5 HIGH via PB10 (I2C)", run_mux_read(tc, *CN8_5, label="PB10 HIGH, PD8=H"))

    resp = dut_cmd(tc, "GPIO_CLEAR PB10", wait=1.0)
    print(f"  GPIO_CLEAR PB10    → DUT: {resp}")
    check_low(r, "CN8-5 LOW  via PB10 (I2C)", run_mux_read(tc, *CN8_5, label="PB10 LOW, PD8=H"))

    dut_cmd(tc, "GPIO_CLEAR PD8", wait=0.5)


def main() -> None:
    ap = argparse.ArgumentParser(description="CN8-5 SCL_Tx analog switch continuity test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN8-5", do_flash=not args.no_flash)
            if ok:
                phase_uart(tc, r)
                phase_i2c(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

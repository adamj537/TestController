#!/usr/bin/env python3
"""test_u13_switch.py — U13 TS5A23157 analog switch continuity test.

Verifies both paths of the U13 analog switch (SS_I2C_UART_ENA / PD8):
  PD8 LOW  → UART path: PA0 → CN8-5 (SCL_Tx),  PA1 → CN8-3 (SDA_Rx)
  PD8 HIGH → I2C path:  PB10 → CN8-5 (SCL_Tx), PB11 → CN8-3 (SDA_Rx)

Each path is exercised HIGH and LOW. No CN3 pogos required.

Usage:
    python3 scripts/test_u13_switch.py
    python3 scripts/test_u13_switch.py --no-flash
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

CN8_3 = (1, 11)   # SDA_Rx
CN8_5 = (1, 13)   # SCL_Tx


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def check_high(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv > HIGH_MV_MIN, mv_str(mv))


def check_low(r: Results, name: str, mv: int | None) -> None:
    r.check(name, mv is not None and mv < LOW_MV_MAX, mv_str(mv))


def phase_uart(tc: TcConsole, r: Results) -> None:
    """PD8=LOW — UART path: PA0→CN8-5, PA1→CN8-3."""
    print("\n── UART path (PD8=LOW) ──────────────────────────────────────────────────")
    print("  (PD8 INPUT PULLDOWN from boot — UART path active by default)")

    for pin, key, label in [
        ("PA0", CN8_5, "CN8-5/PA0"),
        ("PA1", CN8_3, "CN8-3/PA1"),
    ]:
        resp = dut_cmd(tc, f"GPIO_SET {pin} HIGH", wait=1.0)
        print(f"\n  GPIO_SET {pin} HIGH → DUT: {resp}")
        check_high(r, f"{label} HIGH (UART)", run_mux_read(tc, *key, label=f"{pin} HIGH, PD8=L"))

        resp = dut_cmd(tc, f"GPIO_CLEAR {pin}", wait=1.0)
        print(f"  GPIO_CLEAR {pin}    → DUT: {resp}")
        check_low(r, f"{label} LOW  (UART)", run_mux_read(tc, *key, label=f"{pin} LOW, PD8=L"))


def phase_i2c(tc: TcConsole, r: Results) -> None:
    """PD8=HIGH — I2C path: PB10→CN8-5, PB11→CN8-3."""
    print("\n── I2C path (PD8=HIGH) ──────────────────────────────────────────────────")

    resp = dut_cmd(tc, "GPIO_SET PD8 HIGH", wait=1.0)
    print(f"  GPIO_SET PD8 HIGH  → DUT: {resp}")
    time.sleep(0.3)  # switch settle

    for pin, key, label in [
        ("PB11", CN8_3, "CN8-3/PB11"),
        ("PB10", CN8_5, "CN8-5/PB10"),
    ]:
        resp = dut_cmd(tc, f"GPIO_SET {pin} HIGH", wait=1.0)
        print(f"\n  GPIO_SET {pin} HIGH → DUT: {resp}")
        check_high(r, f"{label} HIGH (I2C)", run_mux_read(tc, *key, label=f"{pin} HIGH, PD8=H"))

        resp = dut_cmd(tc, f"GPIO_CLEAR {pin}", wait=1.0)
        print(f"  GPIO_CLEAR {pin}   → DUT: {resp}")
        check_low(r, f"{label} LOW  (I2C)", run_mux_read(tc, *key, label=f"{pin} LOW, PD8=H"))

    dut_cmd(tc, "GPIO_CLEAR PD8", wait=0.5)


def main() -> None:
    ap = argparse.ArgumentParser(description="U13 analog switch continuity test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "U13 switch", do_flash=not args.no_flash)
            if ok:
                phase_uart(tc, r)
                phase_i2c(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

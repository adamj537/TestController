#!/usr/bin/env python3
"""test_cn3_cn8_switch.py — Analog switch (SS_I2C_UART_ENA / PD8) continuity test.

Tests the U13 TS5A23157 analog switch that routes I2C or UART signals to CN8-3/5.

Switch polarity (schematic confirmed):
  PD8 HIGH → I2C path:  PB11 → CN8-3 (SDA_Rx),  PB10 → CN8-5 (SCL_Tx)
  PD8 LOW  → UART path: PA0  → CN8-3 (SDA_Rx),  PA1  → CN8-5 (SCL_Tx)

CN3 mux keys:
  CN3-1  TC_SCL / PB10   MUX1 ch04  (1,4)  — live
  CN3-4  TC_SDA / PB11   MUX1 ch05  (1,5)  — DEAD (HW-011: ESD diode + trace cut)

CN8 mux keys:
  CN8-3  SDA_Rx          MUX1 ch11  (1,11)
  CN8-5  SCL_Tx          MUX1 ch13  (1,13)

Phase 2 — PD8=LOW, I2C path inactive (PB11/PB10 route to CN3 only):
  Drive PB11 HIGH/LOW → CN3-4 follows (DEAD — SKIP); CN8-3 isolated (stays LOW)
  Drive PB10 HIGH/LOW → CN3-1 follows; CN8-5 isolated (stays LOW)
  NOTE: CN3-1 shares the TC I2C SCL bus — HIGH reads during selftest mux will
  show TC I2C activity; result is informational.

Phase 3 — PD8=LOW, UART path active (PA0/PA1 route to CN8):
  Drive PA0 HIGH/LOW → CN8-3 follows
  Drive PA1 HIGH/LOW → CN8-5 follows

Phase 4 — PD8=HIGH, I2C path active (PB11/PB10 route to both CN3 and CN8):
  Drive PB11 HIGH → CN8-3 HIGH, CN3-4 HIGH (DEAD — SKIP)
  Drive PB10 HIGH → CN8-5 HIGH, CN3-1 HIGH
  Drive PB11 LOW  → CN8-3 LOW,  CN3-4 LOW  (DEAD — SKIP)
  Drive PB10 LOW  → CN8-5 LOW,  CN3-1 LOW

Usage:
    python3 scripts/test_cn3_cn8_switch.py
    python3 scripts/test_cn3_cn8_switch.py --no-flash
"""
from __future__ import annotations
import argparse
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, tc_cmd, dut_cmd,                # noqa: E402
                           run_mux_scan, phase_bring_up, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

# ── Mux keys ─────────────────────────────────────────────────────────────────
CN3_1_SCL  = (1,  4)   # PB10 / TC_SCL  — live
CN3_4_SDA  = (1,  5)   # PB11 / TC_SDA  — DEAD (HW-011)
CN8_3_SDA  = (1, 11)   # SDA_Rx via switch
CN8_5_SCL  = (1, 13)   # SCL_Tx via switch

CN3_4_DEAD = True       # HW-011: MUX1 ch05 ESD diode + trace cut


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def check_high(r: Results, name: str, scan: dict, key: tuple, detail: str = "") -> None:
    mv = scan.get(key)
    ok = mv is not None and mv > HIGH_MV_MIN
    r.check(name, ok, f"{mv_str(mv)}{' — ' + detail if detail else ''}")


def check_low(r: Results, name: str, scan: dict, key: tuple) -> None:
    mv = scan.get(key)
    ok = mv is not None and mv < LOW_MV_MAX
    r.check(name, ok, mv_str(mv))


def check_isolated(r: Results, name: str, scan: dict, key: tuple) -> None:
    """Verify a channel stays LOW (isolated) while a different pin is driven HIGH."""
    mv = scan.get(key)
    ok = mv is not None and mv < LOW_MV_MAX
    r.check(name + " (isolated — must stay LOW)", ok, mv_str(mv))


# ── Phase 2: PD8=LOW — I2C pins drive CN3 only; CN8 isolated ─────────────────

def phase_i2c_to_cn3(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: PD8=LOW — I2C pins route to CN3, CN8 isolated ─────────────")
    print("  (PD8 already LOW from boot — no explicit set needed)")

    # ── PB11 HIGH ──
    print("\n  GPIO_SET PB11 HIGH ...")
    resp = dut_cmd(tc, "GPIO_SET PB11 HIGH", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB11 HIGH, PD8=L")
    if CN3_4_DEAD:
        r.skip("CN3-4 TC_SDA/PB11 → HIGH", "HW-011: MUX1 ch05 dead (trace cut)")
    else:
        check_high(r, "CN3-4 TC_SDA/PB11 → HIGH", scan, CN3_4_SDA)
    check_isolated(r, "CN8-3 SDA_Rx", scan, CN8_3_SDA)

    # ── PB11 LOW ──
    print("\n  GPIO_CLEAR PB11 ...")
    resp = dut_cmd(tc, "GPIO_CLEAR PB11", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB11 LOW, PD8=L")
    if CN3_4_DEAD:
        r.skip("CN3-4 TC_SDA/PB11 → LOW", "HW-011: MUX1 ch05 dead (trace cut)")
    else:
        check_low(r, "CN3-4 TC_SDA/PB11 → LOW", scan, CN3_4_SDA)
    check_isolated(r, "CN8-3 SDA_Rx", scan, CN8_3_SDA)

    # ── PB10 HIGH ──
    print("\n  GPIO_SET PB10 HIGH ...")
    resp = dut_cmd(tc, "GPIO_SET PB10 HIGH", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB10 HIGH, PD8=L")
    check_high(r, "CN3-1 TC_SCL/PB10 → HIGH", scan, CN3_1_SCL,
               "TC I2C SCL active during scan — result informational")
    check_isolated(r, "CN8-5 SCL_Tx", scan, CN8_5_SCL)

    # ── PB10 LOW ──
    print("\n  GPIO_CLEAR PB10 ...")
    resp = dut_cmd(tc, "GPIO_CLEAR PB10", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB10 LOW, PD8=L")
    check_low(r, "CN3-1 TC_SCL/PB10 → LOW", scan, CN3_1_SCL)
    check_isolated(r, "CN8-5 SCL_Tx", scan, CN8_5_SCL)


# ── Phase 3: PD8=LOW — UART pins (PA0/PA1) drive CN8 ─────────────────────────

def phase_uart_to_cn8(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: PD8=LOW — UART pins drive CN8 ──────────────────────────────")

    for pin, key, label in [
        ("PA0", CN8_3_SDA, "CN8-3 SDA_Rx/PA0"),
        ("PA1", CN8_5_SCL, "CN8-5 SCL_Tx/PA1"),
    ]:
        print(f"\n  GPIO_SET {pin} HIGH ...")
        resp = dut_cmd(tc, f"GPIO_SET {pin} HIGH", wait=1.0)
        print(f"    DUT: {resp}")
        scan = run_mux_scan(tc, f"{pin} HIGH, PD8=L")
        check_high(r, f"{label} → HIGH", scan, key)

        print(f"\n  GPIO_CLEAR {pin} ...")
        resp = dut_cmd(tc, f"GPIO_CLEAR {pin}", wait=1.0)
        print(f"    DUT: {resp}")
        scan = run_mux_scan(tc, f"{pin} LOW, PD8=L")
        check_low(r, f"{label} → LOW", scan, key)


# ── Phase 4: PD8=HIGH — I2C pins drive both CN8 and CN3 ─────────────────────

def phase_i2c_to_cn8_and_cn3(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 4: PD8=HIGH — I2C pins route to CN8 AND CN3 ───────────────────")

    print("  GPIO_SET PD8 HIGH ...")
    resp = dut_cmd(tc, "GPIO_SET PD8 HIGH", wait=1.0)
    print(f"    DUT: {resp}")
    time.sleep(0.3)  # switch settle

    # ── PB11 HIGH ──
    print("\n  GPIO_SET PB11 HIGH ...")
    resp = dut_cmd(tc, "GPIO_SET PB11 HIGH", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB11 HIGH, PD8=H")
    check_high(r, "CN8-3 SDA_Rx/PB11 → HIGH", scan, CN8_3_SDA)
    if CN3_4_DEAD:
        r.skip("CN3-4 TC_SDA/PB11 → HIGH", "HW-011: MUX1 ch05 dead (trace cut)")
    else:
        check_high(r, "CN3-4 TC_SDA/PB11 → HIGH", scan, CN3_4_SDA)

    # ── PB10 HIGH ──
    print("\n  GPIO_SET PB10 HIGH ...")
    resp = dut_cmd(tc, "GPIO_SET PB10 HIGH", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB10 HIGH, PD8=H")
    check_high(r, "CN8-5 SCL_Tx/PB10 → HIGH", scan, CN8_5_SCL)
    check_high(r, "CN3-1 TC_SCL/PB10 → HIGH", scan, CN3_1_SCL,
               "TC I2C SCL active during scan — result informational")

    # ── PB11 LOW ──
    print("\n  GPIO_CLEAR PB11 ...")
    resp = dut_cmd(tc, "GPIO_CLEAR PB11", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB11 LOW, PD8=H")
    check_low(r, "CN8-3 SDA_Rx/PB11 → LOW", scan, CN8_3_SDA)
    if CN3_4_DEAD:
        r.skip("CN3-4 TC_SDA/PB11 → LOW", "HW-011: MUX1 ch05 dead (trace cut)")
    else:
        check_low(r, "CN3-4 TC_SDA/PB11 → LOW", scan, CN3_4_SDA)

    # ── PB10 LOW ──
    print("\n  GPIO_CLEAR PB10 ...")
    resp = dut_cmd(tc, "GPIO_CLEAR PB10", wait=1.0)
    print(f"    DUT: {resp}")
    scan = run_mux_scan(tc, "PB10 LOW, PD8=H")
    check_low(r, "CN8-5 SCL_Tx/PB10 → LOW", scan, CN8_5_SCL)
    check_low(r, "CN3-1 TC_SCL/PB10 → LOW", scan, CN3_1_SCL)

    # Leave PD8 LOW (safe default)
    dut_cmd(tc, "GPIO_CLEAR PD8", wait=0.5)


# ── Main ─────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="CN3/CN8 analog switch continuity test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN3/CN8 switch", do_flash=not args.no_flash)
            if ok:
                phase_i2c_to_cn3(tc, r)
                phase_uart_to_cn8(tc, r)
                phase_i2c_to_cn8_and_cn3(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

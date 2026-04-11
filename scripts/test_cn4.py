#!/usr/bin/env python3
"""test_cn4.py — CN4 (Pump / Branch 4) functional test.

CN4 carries Branch 4 supply rails for the pump load:
  CN4-1  Pump_Hi  Branch #4 supply output (high side) → MUX1 ch06
  CN4-2  Pump_Lo  Branch #4 low side return            → MUX1 ch08

Phase 1 — DUT bring-up (standard).

Phase 2 — Pump branch enable and rail voltage (CN4-1/2).
  Enable Branch 4 via VIN#4_ENA (DUT PD13 HIGH) and PUMP_ENA transistor
  (DUT PC13 HIGH).  Verify Pump_Hi shows branch supply voltage.
  Verify Pump_Lo reads near GND (low side with no pump load).
  Disable both enables after the check.

Note on Pump_Hi voltage: Branch 4 is an unregulated supply; voltage
depends on the connected load.  ADC128D818 saturates at VREF=3000 mV.
A healthy branch will read 2500–3000 mV at the TIE mux.

Usage:
    python3 scripts/test_cn4.py
    python3 scripts/test_cn4.py --no-flash   # DUT already running pfw
"""
from __future__ import annotations
import argparse
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole
from test_helpers import (
    Results, tc_cmd, dut_cmd, run_mux_scan,
    phase_bring_up, teardown,
)

# ── Thresholds ────────────────────────────────────────────────────────────────
PUMP_HI_MIN_MV = 2000   # Pump_Hi > this when branch is enabled
PUMP_LO_MAX_MV = 500    # Pump_Lo < this (low side near GND with no load)

# ── TIE mux channel keys (mux-channel-map.md) ─────────────────────────────────
MUX_PUMP_HI = (1, 6)
MUX_PUMP_LO = (1, 8)


def phase_pump_branch(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: Pump branch enable and rail verification (CN4-1/2) ─────────")

    # First scan with branch disabled — verify Pump_Hi is not live (no short)
    scan_off = run_mux_scan(tc, "Branch 4 disabled (baseline)")
    hi_off = scan_off.get(MUX_PUMP_HI)
    r.check(
        "Pump_Hi quiescent (branch off)",
        hi_off is not None and hi_off < PUMP_LO_MAX_MV,
        f"{hi_off} mV" if hi_off is not None else "ERR",
    )

    # Enable Branch 4: VIN#4_ENA (PD13) + PUMP_ENA transistor (PC13)
    print("  Enabling VIN#4_ENA (GPIO_SET PD13 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PD13 HIGH", wait=1.0)
    print("  Enabling PUMP_ENA transistor (GPIO_SET PC13 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PC13 HIGH", wait=1.0)
    time.sleep(0.5)   # allow branch to power up

    scan_on = run_mux_scan(tc, "Branch 4 enabled")
    hi_on = scan_on.get(MUX_PUMP_HI)
    lo_on = scan_on.get(MUX_PUMP_LO)

    r.check(
        "CN4-1 Pump_Hi present (branch on)",
        hi_on is not None and hi_on > PUMP_HI_MIN_MV,
        f"{hi_on} mV" if hi_on is not None else "ERR",
    )
    r.check(
        "CN4-2 Pump_Lo near GND (no load)",
        lo_on is not None and lo_on < PUMP_LO_MAX_MV,
        f"{lo_on} mV" if lo_on is not None else "ERR",
    )

    # Disable branch
    print("  Disabling PUMP_ENA transistor (GPIO_CLEAR PC13) ...")
    dut_cmd(tc, "GPIO_CLEAR PC13", wait=1.0)
    print("  Disabling VIN#4_ENA (GPIO_CLEAR PD13) ...")
    dut_cmd(tc, "GPIO_CLEAR PD13", wait=1.0)


def main() -> None:
    ap = argparse.ArgumentParser(description="CN4 (Pump / Branch 4) functional test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN4", do_flash=not args.no_flash)
            if ok:
                phase_pump_branch(tc, r)
        finally:
            teardown(tc)

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

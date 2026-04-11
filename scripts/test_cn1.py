#!/usr/bin/env python3
"""test_cn1.py — CN1 power rail functional test.

CN1 carries the two main power inputs to the G3 board:
  CN1-1  VIN_G3   main supply input → TC applies VDUT1
  CN1-3  VIN#7    battery rail      → TC applies VDUT2

Phase 1 — DUT bring-up (standard).

Phase 2 — Rail voltage verification.
  Enable VDUT2 to simulate the battery rail, then run a TIE mux scan.
  Both rails must read above RAIL_MIN_MV at their respective TIE mux channels.

TIE mux channels (mux-channel-map.md):
  VIN_G3  MUX1 ch09 → key (1, 9)
  VIN#7   MUX1 ch07 → key (1, 7)

Usage:
    python3 scripts/test_cn1.py
    python3 scripts/test_cn1.py --no-flash   # DUT already running pfw
"""
from __future__ import annotations
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole
from test_helpers import (
    Results, tc_cmd, run_mux_scan,
    phase_bring_up, teardown,
    VDUT_MV,
)

# ── Rail voltage thresholds ───────────────────────────────────────────────────
VDUT2_MV   = 3300     # TC simulated battery voltage applied to VIN#7
RAIL_MIN_MV = 2700    # > this = rail present

# ── TIE mux channel keys ──────────────────────────────────────────────────────
MUX_VIN_G3 = (1, 9)
MUX_VIN7   = (1, 7)


def phase_rails(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: Power rail verification (CN1-1, CN1-3) ─────────────────────")

    # Apply VDUT2 to simulate the battery rail (VIN#7)
    print(f"  Enabling VDUT2 at {VDUT2_MV} mV (VIN#7 battery simulation) ...")
    resp = tc_cmd(tc, f"vdac_voltage 2 {VDUT2_MV}", wait=3.0)
    r.check("VDUT2 enabled", f"VDUT2: {VDUT2_MV}" in resp or str(VDUT2_MV) in resp,
            resp.strip())

    scan = run_mux_scan(tc, "CN1 rails")

    vin_g3 = scan.get(MUX_VIN_G3)
    vin7   = scan.get(MUX_VIN7)

    r.check(
        f"VIN_G3 (CN1-1) present",
        vin_g3 is not None and vin_g3 > RAIL_MIN_MV,
        f"{vin_g3} mV" if vin_g3 is not None else "ERR",
    )
    r.check(
        f"VIN#7 (CN1-3) present",
        vin7 is not None and vin7 > RAIL_MIN_MV,
        f"{vin7} mV" if vin7 is not None else "ERR",
    )

    # Turn VDUT2 back off — teardown only does 'vdac off' which covers both
    # channels, but be explicit about state here.
    print(f"  VDUT2 will be disabled at teardown")


def main() -> None:
    ap = argparse.ArgumentParser(description="CN1 power rail functional test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN1", do_flash=not args.no_flash)
            if ok:
                phase_rails(tc, r)
        finally:
            teardown(tc)

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

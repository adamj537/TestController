#!/usr/bin/env python3
"""test_cn5_19.py — CN5-19 (BUTTON_B / PA11) input continuity test.

PA11 is a DUT input.  The TC injects a signal via the U8 DAC mux (ch01)
into the CN5-19 pogo, and the DUT reads back the pin state via PIN_READ.
A TIE mux read of MUX1 ch01 is taken as secondary verification.

Test sequence per cycle:
  1. TC:  mux select 1 1  (inject VDUT1 into CN5-19 via U8 ch01)
  2. DUT: PIN_READ PA11   → must read HIGH
  3. TIE: tie read 1 1    → secondary (must read > HIGH_MV_MIN)
  4. TC:  mux release     (remove injection — pulldown holds PA11 LOW)
  5. DUT: PIN_READ PA11   → must read LOW
  6. TIE: tie read 1 1    → secondary (must read < LOW_MV_MAX)

Usage:
    python3 scripts/test_cn5_19.py
    python3 scripts/test_cn5_19.py --no-flash
    python3 scripts/test_cn5_19.py --no-flash --cycles 10
"""
from __future__ import annotations
import argparse
import re
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, tc_cmd, dut_cmd,                # noqa: E402
                           run_mux_read, phase_bring_up, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

CN5_19 = (1, 1)   # BUTTON_B / PA11 — TIE mux key
U8_CH_BUTTON_B = 1  # U8 HEF4051 ch01 → CN5-19


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def dut_pin_read(tc: TcConsole, pin: str) -> str | None:
    """Send PIN_READ <pin> to DUT; return 'HIGH', 'LOW', or None on error."""
    resp = dut_cmd(tc, f"PIN_READ {pin}", wait=1.0)
    m = re.search(r"OK PIN_READ \S+ (HIGH|LOW)", resp)
    return m.group(1) if m else None


def phase_continuity(tc: TcConsole, r: Results, cycles: int) -> None:
    print(f"\n── CN5-19 BUTTON_B/PA11 input continuity ({cycles} cycle(s)) ──────────────")
    print(f"  U8 ch{U8_CH_BUTTON_B} → CN5-19 pogo → PA11 (input, pulldown)")

    for i in range(cycles):
        if cycles > 1:
            print(f"\n  ── Cycle {i + 1}/{cycles} ──")
        cycle_sfx = f" [cycle {i + 1}]" if cycles > 1 else ""

        # Inject via U8 ch01 → CN5-19
        print(f"  mux select {U8_CH_BUTTON_B} 1  (inject into CN5-19) ...")
        tc_cmd(tc, f"mux select {U8_CH_BUTTON_B} 1", wait=0.5)
        time.sleep(0.1)

        # Primary: DUT reads PA11 — must be HIGH
        state = dut_pin_read(tc, "PA11")
        print(f"  PIN_READ PA11 → {state}")
        r.check(f"PA11 reads HIGH (injected){cycle_sfx}",
                state == "HIGH", state or "no response")

        # Secondary: TIE mux read
        mv = run_mux_read(tc, *CN5_19, label=f"PA11 injected{cycle_sfx}")
        r.check(f"TIE mux CN5-19 HIGH (secondary){cycle_sfx}",
                mv is not None and mv > HIGH_MV_MIN, mv_str(mv))

        # Release injection — pulldown holds PA11 LOW
        print(f"  mux release ...")
        tc_cmd(tc, "mux release", wait=0.5)
        time.sleep(0.1)

        # Primary: DUT reads PA11 — must be LOW
        state = dut_pin_read(tc, "PA11")
        print(f"  PIN_READ PA11 → {state}")
        r.check(f"PA11 reads LOW (released){cycle_sfx}",
                state == "LOW", state or "no response")

        # Secondary: TIE mux read
        mv = run_mux_read(tc, *CN5_19, label=f"PA11 released{cycle_sfx}")
        r.check(f"TIE mux CN5-19 LOW (secondary){cycle_sfx}",
                mv is not None and mv < LOW_MV_MAX, mv_str(mv))


def main() -> None:
    ap = argparse.ArgumentParser(description="CN5-19 BUTTON_B/PA11 input continuity test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    ap.add_argument("--cycles", type=int, default=5,
                    help="Number of inject/release cycles to run (default: 5)")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN5-19", do_flash=not args.no_flash)
            if ok:
                phase_continuity(tc, r, args.cycles)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

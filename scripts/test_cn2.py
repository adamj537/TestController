#!/usr/bin/env python3
"""test_cn2.py — CN2 (Flashlight / LEL Branch 1) functional test.

CN2 carries Branch 1 (5.05V) supply and the Flashlight/LEL sensor signals:
  CN2-1  VBranch#1       Branch 1 supply output → MUX3 ch06
  CN2-2  VREF_DIV        Voltage divider node    → MUX3 ch08
  CN2-3  Flashlight_ENA  DUT PA15 output         → MUX3 ch10
  CN2-4  2611_Vout       Figaro LEL sensor output → MUX3 ch12

Phase 1 — DUT bring-up (standard).

Phase 2 — Flashlight GPIO continuity (CN2-3).
  DUT drives PA15 HIGH then LOW; TC reads TIE mux ch (3,10).

Phase 3 — Branch 1 power test (CN2-1/2).
  Enable Branch 1 via VIN#1_ENA (DUT PD10 HIGH); verify VBranch#1 > BRANCH_MIN_MV.
  VREF_DIV is a resistor-divider node on the branch — read and verify non-zero.
  Disable branch when done.

Phase 4 — LEL sensor output baseline (CN2-4).
  Enable Branch 1 (required for sensor power) and 2611_SENSOR_ENA (DUT PC8 HIGH).
  Read 2611_Vout from TIE mux; verify > SENSOR_MIN_MV (sensor output present).
  Note: sensor warm-up takes ~30s for stable readings; this test verifies presence only.

Usage:
    python3 scripts/test_cn2.py
    python3 scripts/test_cn2.py --no-flash   # DUT already running pfw
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
    HIGH_MV_MIN, LOW_MV_MAX,
)

# ── Thresholds ────────────────────────────────────────────────────────────────
# Branch 1 is 5.05V regulated; TIE mux sees the full rail (no divider listed).
# ADC128D818 saturates at VREF=3000 mV — a healthy 5V branch will read ~2900–3000.
BRANCH_MIN_MV  = 2500   # VBranch#1 > this when enabled (saturates near 3000 mV)
VREF_DIV_MIN   = 500    # Voltage divider node — positive when branch is on
SENSOR_MIN_MV  = 100    # 2611_Vout > this when sensor is powered

# ── TIE mux channel keys (mux-channel-map.md) ─────────────────────────────────
MUX_VBRANCH1  = (3, 6)
MUX_VREF_DIV  = (3, 8)
MUX_FLASH_ENA = (3, 10)
MUX_2611_VOUT = (3, 12)


def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: Flashlight GPIO continuity (CN2-3 / PA15) ──────────────────")

    for level, cmd_str, mv_check in [
        ("HIGH", "GPIO_SET PA15 HIGH",  lambda mv: mv is not None and mv > HIGH_MV_MIN),
        ("LOW",  "GPIO_CLEAR PA15",     lambda mv: mv is not None and mv < LOW_MV_MAX),
    ]:
        print(f"  {cmd_str} ...")
        dut_cmd(tc, cmd_str, wait=1.0)
        time.sleep(0.3)
        scan = run_mux_scan(tc, f"PA15 {level}")
        mv = scan.get(MUX_FLASH_ENA)
        r.check(
            f"CN2-3 Flashlight_ENA/PA15 → {level}",
            mv_check(mv),
            f"{mv} mV" if mv is not None else "ERR",
        )

    # Leave PA15 LOW (flashlight off)
    dut_cmd(tc, "GPIO_CLEAR PA15", wait=0.5)


def phase_branch1_power(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: Branch 1 power test (CN2-1/2) ──────────────────────────────")

    print("  Enabling Branch 1 (GPIO_SET PD10 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PD10 HIGH", wait=1.0)
    time.sleep(0.5)   # allow branch regulator to settle

    scan = run_mux_scan(tc, "Branch 1 enabled")
    vbranch = scan.get(MUX_VBRANCH1)
    vref    = scan.get(MUX_VREF_DIV)

    r.check(
        "CN2-1 VBranch#1 present",
        vbranch is not None and vbranch > BRANCH_MIN_MV,
        f"{vbranch} mV" if vbranch is not None else "ERR",
    )
    r.check(
        "CN2-2 VREF_DIV non-zero",
        vref is not None and vref > VREF_DIV_MIN,
        f"{vref} mV" if vref is not None else "ERR",
    )

    print("  Disabling Branch 1 (GPIO_CLEAR PD10) ...")
    dut_cmd(tc, "GPIO_CLEAR PD10", wait=1.0)


def phase_lel_sensor(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 4: LEL sensor output baseline (CN2-4) ─────────────────────────")

    # Branch 1 required for sensor power
    print("  Enabling Branch 1 (GPIO_SET PD10 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PD10 HIGH", wait=1.0)
    time.sleep(0.3)

    print("  Enabling 2611_SENSOR_ENA (GPIO_SET PC8 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PC8 HIGH", wait=1.0)
    # Note: full warm-up takes ~30s; here we just verify power is reaching the sensor
    time.sleep(1.0)

    scan = run_mux_scan(tc, "2611 sensor powered")
    vout = scan.get(MUX_2611_VOUT)
    r.check(
        "CN2-4 2611_Vout present (sensor powered)",
        vout is not None and vout > SENSOR_MIN_MV,
        f"{vout} mV" if vout is not None else "ERR",
    )

    print("  Disabling 2611_SENSOR_ENA (GPIO_CLEAR PC8) ...")
    dut_cmd(tc, "GPIO_CLEAR PC8", wait=1.0)
    print("  Disabling Branch 1 (GPIO_CLEAR PD10) ...")
    dut_cmd(tc, "GPIO_CLEAR PD10", wait=1.0)


def main() -> None:
    ap = argparse.ArgumentParser(description="CN2 (Flashlight/LEL Branch 1) functional test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN2", do_flash=not args.no_flash)
            if ok:
                phase_gpio_continuity(tc, r)
                phase_branch1_power(tc, r)
                phase_lel_sensor(tc, r)
        finally:
            teardown(tc)

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

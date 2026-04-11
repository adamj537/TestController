#!/usr/bin/env python3
"""test_cn5.py — CN5 (Breakout) functional test.

CN5 is the main breakout connector carrying GPIO outputs, regulated supply
rails, button inputs, and sounder/LED signals.

Phase 1 — DUT bring-up (standard).

Phase 2 — DUT GPIO output continuity (Scenario A).
  Drive each output HIGH then LOW; TC reads via TIE mux.
  All 16 outputs must swing > HIGH_MV_MIN when HIGH and < LOW_MV_MAX when LOW.

  DUT output pins:
    PE5  LED_B          → (3, 3)
    PD7  SOUNDER_gain1  → (3, 1)
    PD9  SOUNDER_gain2  → (2, 14)
    PB3  SOUNDER_volume → (0, 9)
    PC7  LED_R          → (0, 7)
    PB4  LED_G          → (0, 5)
    PE6  DISP_LED_ENA   → (0, 0)
    PD1  LCD_SPI2_SCK   → (0, 6)
    PB15 LCD_SPI2_MOSI  → (3, 2)
    PD12 LCD_EXTCOMIN   → (3, 0)
    PE13 LCD_SPI2_CS_H  → (0, 12)
    PC4  LCD_DISP_ENA   → (0, 10)
    PC0  MAIN_I2C3_SCL  → (0, 8)
    PC1  MAIN_I2C3_SDA  → (1, 0)
    PA11 BUTTON_B       → (1, 1)
    PA12 BUTTON_C       → (0, 14)

Phase 3 — Regulated supply rails.
  3V_Branch#5  (0, 4) — DUT 3V regulated, present when DUT is on.
  5V_Branch#5  (0, 13) — DUT 5V boost output.
  VBranch#2    (3, 4) — Branch 2 supply (enable via VIN#2_ENA = PD11).
  PB-A/PWR-ON  (0, 11) — verify KEEPALIVE holds this HIGH while DUT is running.

Phase 4 — SOUNDER_audio baseline (0, 3).
  Read without driving — confirms pogo contact to audio net.

Usage:
    python3 scripts/test_cn5.py
    python3 scripts/test_cn5.py --no-flash   # DUT already running pfw
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
RAIL_3V_MIN   = 2700    # 3V_Branch#5 > this (3.3V regulated)
RAIL_5V_MIN   = 4500    # 5V_Branch#5 > this (5V boost output)
VBRANCH2_MIN  = 2500    # VBranch#2 > this when enabled (4.1V; ADC ref=3V, saturates)
PBA_MIN_MV    = 2700    # PB-A must be HIGH while KEEPALIVE is active

# ── DUT GPIO output continuity table ──────────────────────────────────────────
# (mux_key, dut_pin, signal, connector_pin)
CN5_OUTPUTS: list[tuple[tuple[int, int], str, str, str]] = [
    ((3, 3),  "PE5",  "LED_B",         "CN5-4"),
    ((3, 1),  "PD7",  "SOUNDER_gain1", "CN5-5"),
    ((2, 14), "PD9",  "SOUNDER_gain2", "CN5-6"),
    ((0, 9),  "PB3",  "SOUNDER_volume","CN5-7"),
    ((0, 7),  "PC7",  "LED_R",         "CN5-8"),
    ((0, 5),  "PB4",  "LED_G",         "CN5-9"),
    ((0, 0),  "PE6",  "DISP_LED_ENA",  "CN5-14"),
    ((0, 6),  "PD1",  "LCD_SPI2_SCK",  "CN5-16"),
    ((3, 2),  "PB15", "LCD_SPI2_MOSI", "CN5-17"),
    ((3, 0),  "PD12", "LCD_EXTCOMIN",  "CN5-18"),
    ((0, 12), "PE13", "LCD_SPI2_CS_H", "CN5-23"),
    ((0, 10), "PC4",  "LCD_DISP_ENA",  "CN5-26"),
    ((0, 8),  "PC0",  "MAIN_I2C3_SCL", "CN5-15"),
    ((1, 0),  "PC1",  "MAIN_I2C3_SDA", "CN5-20"),
    ((1, 1),  "PA11", "BUTTON_B",      "CN5-19"),
    ((0, 14), "PA12", "BUTTON_C",      "CN5-21"),
]


def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: DUT GPIO output continuity (CN5 outputs) ───────────────────")

    for level, mv_check in [
        ("HIGH", lambda mv: mv is not None and mv > HIGH_MV_MIN),
        ("LOW",  lambda mv: mv is not None and mv < LOW_MV_MAX),
    ]:
        print(f"\n  Driving all CN5 outputs {level} ...")
        for _, pin, _, _ in CN5_OUTPUTS:
            cmd_str = f"GPIO_SET {pin} HIGH" if level == "HIGH" else f"GPIO_CLEAR {pin}"
            resp = dut_cmd(tc, cmd_str, wait=0.5)
            print(f"    {cmd_str}: {resp}")

        time.sleep(0.3)
        scan = run_mux_scan(tc, f"CN5 outputs {level}")

        for mux_key, pin, signal, conn in CN5_OUTPUTS:
            mv = scan.get(mux_key)
            r.check(
                f"{conn} {signal}/{pin} → {level}",
                mv_check(mv),
                f"{mv} mV" if mv is not None else "ERR",
            )


def phase_supply_rails(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: Supply rail verification (CN5-1/13/22/25) ──────────────────")

    # Branch 2 enable (PD11) — 4.1V regulated for sounder/LEDs
    print("  Enabling Branch 2 (GPIO_SET PD11 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PD11 HIGH", wait=1.0)
    time.sleep(0.5)

    scan = run_mux_scan(tc, "CN5 supply rails")

    v3   = scan.get((0, 4))
    v5   = scan.get((0, 13))
    vb2  = scan.get((3, 4))
    pba  = scan.get((0, 11))

    r.check(
        "CN5-13 3V_Branch#5 present",
        v3 is not None and v3 > RAIL_3V_MIN,
        f"{v3} mV" if v3 is not None else "ERR",
    )
    r.check(
        "CN5-22 5V_Branch#5 present",
        v5 is not None and v5 > RAIL_5V_MIN,
        f"{v5} mV" if v5 is not None else "ERR",
    )
    r.check(
        "CN5-1 VBranch#2 present (Branch 2 on)",
        vb2 is not None and vb2 > VBRANCH2_MIN,
        f"{vb2} mV" if vb2 is not None else "ERR",
    )
    r.check(
        "CN5-25 PB-A HIGH (KEEPALIVE active)",
        pba is not None and pba > PBA_MIN_MV,
        f"{pba} mV" if pba is not None else "ERR",
    )

    print("  Disabling Branch 2 (GPIO_CLEAR PD11) ...")
    dut_cmd(tc, "GPIO_CLEAR PD11", wait=1.0)


def phase_sounder_baseline(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 4: SOUNDER_audio baseline (CN5-10) ─────────────────────────────")
    scan = run_mux_scan(tc, "SOUNDER_audio baseline")
    mv = scan.get((0, 3))
    # Just verify pogo contact — audio net has no specific quiescent voltage,
    # ERR means no contact.
    r.check(
        "CN5-10 SOUNDER_audio readable (pogo contact)",
        mv is not None,
        f"{mv} mV" if mv is not None else "ERR — no contact",
    )


def main() -> None:
    ap = argparse.ArgumentParser(description="CN5 (Breakout) functional test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN5", do_flash=not args.no_flash)
            if ok:
                phase_gpio_continuity(tc, r)
                phase_supply_rails(tc, r)
                phase_sounder_baseline(tc, r)
        finally:
            teardown(tc)

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""test_cn5.py — CN5 (Breakout) functional test.

CN5 is the main breakout connector carrying GPIO outputs, regulated supply
rails, button inputs, and sounder/LED signals.

Pogo population (this fixture):
  Loaded:     CN5-4..10, 14..21, 23, 25, 26
  Not loaded: CN5-1, 2, 11, 12, 13, 22, 24

Phase 1 — DUT bring-up (standard).
  Asserts CN5-25 (PB-A/PWR-ON) via TIE mux to latch KEEPALIVE and power up
  the DUT.  If Phase 1 completes (DUT heartbeat + ENTER_TEST), CN5-25 is
  confirmed functional.  No separate pass/fail check is needed.

Phase 2 — DUT GPIO output continuity (CN5-4..9, 14..21, 23, 26).
  Per-pin: drive HIGH → read TIE mux → drive LOW → read TIE mux.
  16 outputs must swing > HIGH_MV_MIN when HIGH and < LOW_MV_MAX when LOW.

  DUT output pins:
    PE5  LED_B          CN5-4   → (3, 3)
    PD7  SOUNDER_gain1  CN5-5   → (3, 1)
    PD9  SOUNDER_gain2  CN5-6   → (2, 14)
    PB3  SOUNDER_volume CN5-7   → (0, 9)
    PC7  LED_R          CN5-8   → (0, 7)
    PB4  LED_G          CN5-9   → (0, 5)
    PE6  DISP_LED_ENA   CN5-14  → (0, 0)
    PC0  MAIN_I2C3_SCL  CN5-15  → (0, 8)
    PD1  LCD_SPI2_SCK   CN5-16  → (0, 6)
    PB15 LCD_SPI2_MOSI  CN5-17  → (3, 2)
    PD12 LCD_EXTCOMIN   CN5-18  → (3, 0)
    PA11 BUTTON_B       CN5-19  → (1, 1)
    PC1  MAIN_I2C3_SDA  CN5-20  → (1, 0)
    PA12 BUTTON_C       CN5-21  → (0, 14)
    PE13 LCD_SPI2_CS_H  CN5-23  → (0, 12)
    PC4  LCD_DISP_ENA   CN5-26  → (0, 10)

Phase 3 — SOUNDER_audio AND gate continuity (CN5-10).
  U12 AND gate: SOUNDER_audio = PA2 AND PE1.
  Three states exercise both inputs and the combined output:
    PA2=H, PE1=H → CN5-10 HIGH  (output enabled)
    PA2=L, PE1=H → CN5-10 LOW   (PA2 input path)
    PA2=H, PE1=L → CN5-10 LOW   (PE1 input path)

  Supply rails CN5-1 (VBranch#2), CN5-13 (3V_Branch#5), CN5-22 (5V_Branch#5)
  are not tested — pogos not loaded.

Usage:
    python3 scripts/test_cn5.py
    python3 scripts/test_cn5.py --no-flash   # DUT already running pfw
"""
from __future__ import annotations
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, dut_cmd,                        # noqa: E402
                           run_mux_read, phase_bring_up, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

# ── DUT GPIO output continuity table ─────────────────────────────────────────
# (mux_key, dut_pin, signal, connector_pin)
CN5_OUTPUTS: list[tuple[tuple[int, int], str, str, str]] = [
    ((3, 3),  "PE5",  "LED_B",         "CN5-4"),
    ((3, 1),  "PD7",  "SOUNDER_gain1", "CN5-5"),
    ((2, 14), "PD9",  "SOUNDER_gain2", "CN5-6"),
    ((0, 9),  "PB3",  "SOUNDER_volume","CN5-7"),
    ((0, 7),  "PC7",  "LED_R",         "CN5-8"),
    ((0, 5),  "PB4",  "LED_G",         "CN5-9"),
    ((0, 0),  "PE6",  "DISP_LED_ENA",  "CN5-14"),
    ((0, 8),  "PC0",  "MAIN_I2C3_SCL", "CN5-15"),
    ((0, 6),  "PD1",  "LCD_SPI2_SCK",  "CN5-16"),
    ((3, 2),  "PB15", "LCD_SPI2_MOSI", "CN5-17"),
    ((3, 0),  "PD12", "LCD_EXTCOMIN",  "CN5-18"),
    ((1, 1),  "PA11", "BUTTON_B",      "CN5-19"),
    ((1, 0),  "PC1",  "MAIN_I2C3_SDA", "CN5-20"),
    ((0, 14), "PA12", "BUTTON_C",      "CN5-21"),
    ((0, 12), "PE13", "LCD_SPI2_CS_H", "CN5-23"),
    ((0, 10), "PC4",  "LCD_DISP_ENA",  "CN5-26"),
]

CN5_AUDIO = (0, 3)   # CN5-10 SOUNDER_audio — U12 AND gate output


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


# ── Phase 2: DUT GPIO output continuity ──────────────────────────────────────

def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: DUT GPIO output continuity (CN5-4..9, 14..21, 23, 26) ──────")

    for mux_key, pin, signal, conn in CN5_OUTPUTS:
        print(f"\n  GPIO_SET {pin} HIGH ...")
        resp = dut_cmd(tc, f"GPIO_SET {pin} HIGH", wait=1.0)
        print(f"    {resp}")
        mv = run_mux_read(tc, *mux_key, label=f"{conn}/{pin} HIGH")
        r.check(f"{conn} {signal}/{pin} → HIGH",
                mv is not None and mv > HIGH_MV_MIN, mv_str(mv))

        print(f"\n  GPIO_CLEAR {pin} ...")
        resp = dut_cmd(tc, f"GPIO_CLEAR {pin}", wait=1.0)
        print(f"    {resp}")
        mv = run_mux_read(tc, *mux_key, label=f"{conn}/{pin} LOW")
        r.check(f"{conn} {signal}/{pin} → LOW",
                mv is not None and mv < LOW_MV_MAX, mv_str(mv))


# ── Phase 3: SOUNDER_audio AND gate continuity ───────────────────────────────

def phase_sounder_and_gate(tc: TcConsole, r: Results) -> None:
    """U12 AND gate: SOUNDER_audio (CN5-10) = PA2 AND PE1.

    Three states verify both input paths and the combined output.
    Leaves PA2 and PE1 LOW on exit.
    """
    print("\n── Phase 3: SOUNDER_audio AND gate continuity (CN5-10) ─────────────────")
    print("  U12: SOUNDER_audio = PA2 AND PE1")

    # PA2=H, PE1=H → output HIGH
    print("\n  GPIO_SET PA2 HIGH, GPIO_SET PE1 HIGH ...")
    dut_cmd(tc, "GPIO_SET PA2 HIGH", wait=1.0)
    dut_cmd(tc, "GPIO_SET PE1 HIGH", wait=1.0)
    mv = run_mux_read(tc, *CN5_AUDIO, label="PA2=H PE1=H")
    r.check("CN5-10 SOUNDER_audio PA2=H·PE1=H → HIGH",
            mv is not None and mv > HIGH_MV_MIN, mv_str(mv))

    # PA2=L, PE1=H → output LOW (PA2 input path)
    print("\n  GPIO_CLEAR PA2 (PE1 stays HIGH) ...")
    dut_cmd(tc, "GPIO_CLEAR PA2", wait=1.0)
    mv = run_mux_read(tc, *CN5_AUDIO, label="PA2=L PE1=H")
    r.check("CN5-10 SOUNDER_audio PA2=L·PE1=H → LOW",
            mv is not None and mv < LOW_MV_MAX, mv_str(mv))

    # PA2=H, PE1=L → output LOW (PE1 input path)
    print("\n  GPIO_SET PA2 HIGH, GPIO_CLEAR PE1 ...")
    dut_cmd(tc, "GPIO_SET PA2 HIGH", wait=1.0)
    dut_cmd(tc, "GPIO_CLEAR PE1", wait=1.0)
    mv = run_mux_read(tc, *CN5_AUDIO, label="PA2=H PE1=L")
    r.check("CN5-10 SOUNDER_audio PA2=H·PE1=L → LOW",
            mv is not None and mv < LOW_MV_MAX, mv_str(mv))

    # Leave both LOW
    dut_cmd(tc, "GPIO_CLEAR PA2", wait=0.5)


# ── Main ──────────────────────────────────────────────────────────────────────

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
                phase_sounder_and_gate(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

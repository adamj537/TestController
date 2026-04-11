#!/usr/bin/env python3
"""test_cn7.py — CN7 (BT/GPS) functional test.

CN7 carries BT/GPS radio module signals: SPI, I2C, UART, GPIO, and Vradio.

Pogo population (this fixture):
  Loaded:     CN7-5..14, 16, 18..20
  Not loaded: CN7-1, 2, 3, 4

Phase 1 — DUT bring-up (standard).

Phase 2 — DUT GPIO output continuity (CN7-5..14, 16, 18..20).
  Per-pin: drive HIGH → read TIE mux → drive LOW → read TIE mux.
  14 outputs must swing > HIGH_MV_MIN when HIGH and < LOW_MV_MAX when LOW.

  DUT output pins:
    PB9  BT/GPS_I2C1_SDA  CN7-5   → (2, 12)
    PB8  BT/GPS_I2C1_SCL  CN7-6   → (2, 10)
    PD0  GPS_SPI3_CS      CN7-7   → (2, 11)
    PE8  BT_GPIO2         CN7-8   → (2, 8)
    PE7  BT_GPIO1         CN7-9   → (2, 9)
    PE4  BT_SPI3_CS       CN7-10  → (2, 6)
    PD6  BT_USART2_RX     CN7-11  → (2, 7)
    PD5  BT_USART2_TX     CN7-12  → (2, 4)
    PD4  BT_USART2_RTS    CN7-13  → (3, 5)
    PD3  BT_USART2_CTS    CN7-14  → (2, 5)
    PC12 BT/GPS_SPI3_MOSI CN7-16  → (3, 7)
    PC10 SPI3_SCK/GPS_GPIO1 CN7-18 → (3, 11)
    PB5  GPS_GPIO2        CN7-19  → (3, 14)
    PC11 BT/GPS_SPI3_MISO CN7-20  → (3, 15)

  SKIPPED (CMD UART — must not be disturbed):
    PB6  GPS_USART1_TX  → (3, 9)   ← DUT UART TX
    PB7  GPS_USART1_RX  → (3, 13)  ← DUT UART RX

Phase 3 — Vradio supply voltage (CN7-1/2).
  Not tested — pogos not loaded.

Usage:
    python3 scripts/test_cn7.py
    python3 scripts/test_cn7.py --no-flash   # DUT already running pfw
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
CN7_OUTPUTS: list[tuple[tuple[int, int], str, str, str]] = [
    ((2, 12), "PB9",  "BT/GPS_I2C1_SDA",   "CN7-5"),
    ((2, 10), "PB8",  "BT/GPS_I2C1_SCL",   "CN7-6"),
    ((2, 11), "PD0",  "GPS_SPI3_CS",        "CN7-7"),
    ((2, 8),  "PE8",  "BT_GPIO2",           "CN7-8"),
    ((2, 9),  "PE7",  "BT_GPIO1",           "CN7-9"),
    ((2, 6),  "PE4",  "BT_SPI3_CS",         "CN7-10"),
    ((2, 7),  "PD6",  "BT_USART2_RX",       "CN7-11"),
    ((2, 4),  "PD5",  "BT_USART2_TX",       "CN7-12"),
    ((3, 5),  "PD4",  "BT_USART2_RTS",      "CN7-13"),
    ((2, 5),  "PD3",  "BT_USART2_CTS",      "CN7-14"),
    ((3, 7),  "PC12", "BT/GPS_SPI3_MOSI",   "CN7-16"),
    ((3, 11), "PC10", "SPI3_SCK/GPS_GPIO1", "CN7-18"),
    ((3, 14), "PB5",  "GPS_GPIO2",          "CN7-19"),
    ((3, 15), "PC11", "BT/GPS_SPI3_MISO",   "CN7-20"),
]


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


# ── Phase 2: DUT GPIO output continuity ──────────────────────────────────────

def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: DUT GPIO output continuity (CN7-5..14, 16, 18..20) ─────────")
    print("  SKIP: PB6 GPS_USART1_TX, PB7 GPS_USART1_RX — DUT CMD UART")

    for mux_key, pin, signal, conn in CN7_OUTPUTS:
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


# ── Phase 3: Vradio supply voltage ───────────────────────────────────────────

def phase_vradio(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: Vradio supply voltage (CN7-1/2) ─────────────────────────────")
    r.skip("CN7-1/2 Vradio", "pogo not loaded")


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description="CN7 (BT/GPS) functional test")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN7", do_flash=not args.no_flash)
            if ok:
                phase_gpio_continuity(tc, r)
                phase_vradio(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

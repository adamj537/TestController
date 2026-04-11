#!/usr/bin/env python3
"""test_cn7.py — CN7 (BT/GPS) functional test.

CN7 carries BT/GPS radio module signals: SPI, I2C, UART, GPIO, and Vradio.

Phase 1 — DUT bring-up (standard).

Phase 2 — DUT GPIO output continuity (Scenario A).
  Drive each output HIGH then LOW; TC reads via TIE mux.
  All 14 outputs must swing > HIGH_MV_MIN HIGH and < LOW_MV_MAX LOW.

  DUT output pins and TIE mux keys (mux-channel-map.md):
    PE8  BT_GPIO2           → (2, 8)
    PE7  BT_GPIO1           → (2, 9)
    PE3  BT_SPI3_CS         → (2, 6)
    PD6  BT_USART2_RX       → (2, 7)
    PD5  BT_USART2_TX       → (2, 4)
    PD4  BT_USART2_RTS      → (3, 5)
    PD3  BT_USART2_CTS      → (2, 5)
    PE14 BT/GPS_SPI3_MOSI   → (3, 7)
    PE15 BT/GPS_SPI3_MISO   → (3, 15)
    PC10 SPI3_SCK/GPS_GPIO1 → (3, 11)
    PB5  GPS_GPIO2          → (3, 14)
    PB8  BT/GPS_I2C1_SCL    → (2, 10)
    PB9  BT/GPS_I2C1_SDA    → (2, 12)
    PD0  GPS_SPI3_CS        → (2, 11)

  SKIPPED (CMD UART — DUT UART console):
    PB6  GPS_USART1_TX → (3, 9)   ← DUT TX, must not be disturbed
    PB7  GPS_USART1_RX → (3, 13)  ← DUT RX, must not be disturbed

Phase 3 — Vradio supply voltage (CN7-1/2).
  Read Vradio (MUX2 ch13) → key (2, 13).
  Vradio is a regulated supply to the BT/GPS module.  Verify > VRADIO_MIN_MV
  (supply present when DUT is powered).

Usage:
    python3 scripts/test_cn7.py
    python3 scripts/test_cn7.py --no-flash   # DUT already running pfw
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
VRADIO_MIN_MV = 2700   # Vradio > this when DUT is powered (regulated 3.3V)

# ── DUT GPIO output continuity table ──────────────────────────────────────────
# (mux_key, dut_pin, signal, connector_pin)
CN7_OUTPUTS: list[tuple[tuple[int, int], str, str, str]] = [
    ((2, 8),  "PE8",  "BT_GPIO2",          "CN7-8"),
    ((2, 9),  "PE7",  "BT_GPIO1",          "CN7-9"),
    ((2, 6),  "PE3",  "BT_SPI3_CS",        "CN7-10"),
    ((2, 7),  "PD6",  "BT_USART2_RX",      "CN7-11"),
    ((2, 4),  "PD5",  "BT_USART2_TX",      "CN7-12"),
    ((3, 5),  "PD4",  "BT_USART2_RTS",     "CN7-13"),
    ((2, 5),  "PD3",  "BT_USART2_CTS",     "CN7-14"),
    ((3, 7),  "PE14", "BT/GPS_SPI3_MOSI",  "CN7-16"),
    ((3, 15), "PE15", "BT/GPS_SPI3_MISO",  "CN7-20"),
    ((3, 11), "PC10", "SPI3_SCK/GPS_GPIO1","CN7-18"),
    ((3, 14), "PB5",  "GPS_GPIO2",         "CN7-19"),
    ((2, 10), "PB8",  "BT/GPS_I2C1_SCL",  "CN7-6"),
    ((2, 12), "PB9",  "BT/GPS_I2C1_SDA",  "CN7-5"),
    ((2, 11), "PD0",  "GPS_SPI3_CS",       "CN7-7"),
]

# CMD UART pins — never drive these, DUT console uses them
_CMD_UART_SKIP = {
    "PB6": "GPS_USART1_TX (CMD UART)",
    "PB7": "GPS_USART1_RX (CMD UART)",
}


def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: DUT GPIO output continuity (CN7 outputs) ───────────────────")
    print(f"  SKIP: {', '.join(_CMD_UART_SKIP.values())} — DUT UART console")

    for level, mv_check in [
        ("HIGH", lambda mv: mv is not None and mv > HIGH_MV_MIN),
        ("LOW",  lambda mv: mv is not None and mv < LOW_MV_MAX),
    ]:
        print(f"\n  Driving all CN7 outputs {level} ...")
        for _, pin, _, _ in CN7_OUTPUTS:
            cmd_str = f"GPIO_SET {pin} HIGH" if level == "HIGH" else f"GPIO_CLEAR {pin}"
            resp = dut_cmd(tc, cmd_str, wait=0.5)
            print(f"    {cmd_str}: {resp}")

        time.sleep(0.3)
        scan = run_mux_scan(tc, f"CN7 outputs {level}")

        for mux_key, pin, signal, conn in CN7_OUTPUTS:
            mv = scan.get(mux_key)
            r.check(
                f"{conn} {signal}/{pin} → {level}",
                mv_check(mv),
                f"{mv} mV" if mv is not None else "ERR",
            )


def phase_vradio(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: Vradio supply voltage (CN7-1/2) ─────────────────────────────")
    scan = run_mux_scan(tc, "Vradio")
    mv = scan.get((2, 13))
    r.check(
        "CN7-1/2 Vradio present",
        mv is not None and mv > VRADIO_MIN_MV,
        f"{mv} mV" if mv is not None else "ERR",
    )


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

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

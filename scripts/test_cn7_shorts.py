#!/usr/bin/env python3
"""test_cn7_shorts.py — CN7 inter-pin short detection.

For each DUT output pin on CN7, drives it HIGH and reads all other
CN7 channels. Any neighbor reading above SHORT_MV_THRESHOLD above
its idle baseline indicates a solder bridge or trace short. Then
drives LOW and verifies it returns.

PB9/PB8 are I2C SDA/SCL with hardware pull-ups — the baseline-delta
approach handles these without preamble or false SHORT hits.

Usage:
    python3 scripts/test_cn7_shorts.py
    python3 scripts/test_cn7_shorts.py --no-flash
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

# ── Short detection threshold ─────────────────────────────────────────────────
SHORT_MV_THRESHOLD = 500

# ── CN7 output pins ───────────────────────────────────────────────────────────
# (mux_key, dut_pin, signal, connector_pin)
# SKIPPED: PB6 GPS_USART1_TX → (3,9), PB7 GPS_USART1_RX → (3,13) — CMD UART
CN7_PINS: list[tuple[tuple[int, int], str, str, str]] = [
    ((2, 12), "PB9",  "BT/GPS_I2C1_SDA",     "CN7-5"),
    ((2, 10), "PB8",  "BT/GPS_I2C1_SCL",     "CN7-6"),
    ((2, 11), "PD0",  "GPS_SPI3_CS",          "CN7-7"),
    ((2, 8),  "PE8",  "BT_GPIO2",             "CN7-8"),
    ((2, 9),  "PE7",  "BT_GPIO1",             "CN7-9"),
    ((2, 6),  "PE4",  "BT_SPI3_CS",           "CN7-10"),
    ((2, 7),  "PD6",  "BT_USART2_RX",         "CN7-11"),
    ((2, 4),  "PD5",  "BT_USART2_TX",         "CN7-12"),
    ((3, 5),  "PD4",  "BT_USART2_RTS",        "CN7-13"),
    ((2, 5),  "PD3",  "BT_USART2_CTS",        "CN7-14"),
    ((3, 7),  "PC12", "BT/GPS_SPI3_MOSI",     "CN7-16"),
    ((3, 11), "PC10", "SPI3_SCK/GPS_GPIO1",   "CN7-18"),
    ((3, 14), "PB5",  "GPS_GPIO2",            "CN7-19"),
    ((3, 15), "PC11", "BT/GPS_SPI3_MISO",     "CN7-20"),
]


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def phase_short_detect(tc: TcConsole, r: Results) -> None:
    print("\n── CN7 inter-pin short detection ───────────────────────────────────────")
    print(f"  Short threshold: {SHORT_MV_THRESHOLD} mV delta above idle baseline on any non-driven pin")
    print("  SKIP: PB6 GPS_USART1_TX, PB7 GPS_USART1_RX — DUT CMD UART")

    # Measure idle baseline for every pin before driving anything.
    # PB9/PB8 (I2C SDA/SCL) sit at ~2340 mV at boot due to pull-ups —
    # delta-from-baseline avoids false SHORT hits on those pins.
    print("  Reading idle baseline ...")
    baseline: dict[tuple[int, int], int] = {}
    for mux_key, _, _, conn in CN7_PINS:
        mv = run_mux_read(tc, *mux_key, label=f"{conn} baseline")
        baseline[mux_key] = mv if mv is not None else 0
        print(f"    {conn}: {baseline[mux_key]} mV")

    for driven_mux, driven_pin, driven_sig, driven_conn in CN7_PINS:
        print(f"\n  ── Drive {driven_conn} {driven_sig}/{driven_pin} HIGH ──")
        dut_cmd(tc, f"GPIO_SET {driven_pin} HIGH", wait=1.0)

        # Verify driven pin is HIGH
        mv_self = run_mux_read(tc, *driven_mux, label=f"{driven_conn} driven HIGH")
        self_ok = mv_self is not None and mv_self > HIGH_MV_MIN
        r.check(f"{driven_conn} {driven_pin} → HIGH",
                self_ok, mv_str(mv_self))
        if not self_ok:
            print(f"    WARNING: driven pin not reading HIGH — skipping neighbor check")
            dut_cmd(tc, f"GPIO_CLEAR {driven_pin}", wait=0.5)
            continue

        # Check all other pins for voltage ABOVE their idle baseline
        for nbr_mux, nbr_pin, nbr_sig, nbr_conn in CN7_PINS:
            if nbr_mux == driven_mux:
                continue
            mv_nbr = run_mux_read(tc, *nbr_mux, label=f"{nbr_conn} while {driven_pin}=H")
            nbr_base = baseline.get(nbr_mux, 0)
            delta = (mv_nbr - nbr_base) if mv_nbr is not None else 0
            no_short = delta < SHORT_MV_THRESHOLD
            if not no_short:
                print(f"    SHORT: {nbr_conn} {nbr_sig} reads {mv_nbr} mV "
                      f"(+{delta} above baseline {nbr_base} mV) "
                      f"while {driven_conn} {driven_pin} is HIGH")
            r.check(
                f"SHORT {driven_conn}→{nbr_conn} ({driven_pin}↔{nbr_pin})",
                no_short,
                f"{mv_nbr} mV (+{delta})" if mv_nbr is not None else "ERR",
            )

        # Drive LOW and verify
        dut_cmd(tc, f"GPIO_CLEAR {driven_pin}", wait=0.5)
        mv_low = run_mux_read(tc, *driven_mux, label=f"{driven_conn} released LOW")
        r.check(f"{driven_conn} {driven_pin} → LOW",
                mv_low is not None and mv_low < LOW_MV_MAX, mv_str(mv_low))


def main() -> None:
    ap = argparse.ArgumentParser(description="CN7 inter-pin short detection")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN7 shorts", do_flash=not args.no_flash)
            if ok:
                phase_short_detect(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""test_cn5_shorts.py — CN5 inter-pin short detection.

For each DUT output pin on CN5, drives it HIGH and reads all other
CN5 channels. Any neighbor reading above SHORT_MV_THRESHOLD indicates
a solder bridge or trace short. Then drives LOW and verifies it returns.

MUX1 channels (ch00/ch01) are excluded — U11 is blown on this fixture.

Usage:
    python3 scripts/test_cn5_shorts.py
    python3 scripts/test_cn5_shorts.py --no-flash
"""
from __future__ import annotations
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, tc_cmd, dut_cmd,                # noqa: E402
                           run_mux_read, phase_bring_up, teardown,
                           HIGH_MV_MIN, LOW_MV_MAX)

# ── Short detection threshold ─────────────────────────────────────────────────
# A non-driven pin reading above this while another pin is HIGH = short suspect.
SHORT_MV_THRESHOLD = 500

# ── CN5 output pins with working MUX channels (MUX1 excluded) ────────────────
# (mux_key, dut_pin, signal, connector_pin)
CN5_PINS: list[tuple[tuple[int, int], str, str, str]] = [
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
    ((0, 14), "PA12", "BUTTON_C",      "CN5-21"),
    ((0, 12), "PE13", "LCD_SPI2_CS_H", "CN5-23"),
    ((0, 10), "PC4",  "LCD_DISP_ENA",  "CN5-26"),
]

# SOUNDER_audio is an AND gate output — read-only, not driven directly
CN5_AUDIO = ((0, 3), "—",    "SOUNDER_audio", "CN5-10")


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


def phase_short_detect(tc: TcConsole, r: Results) -> None:
    print("\n── CN5 inter-pin short detection (MUX1 excluded) ───────────────────────")
    print(f"  Short threshold: {SHORT_MV_THRESHOLD} mV on any non-driven pin")

    # Pre-drive all output pins LOW so pull-ups/peripheral state don't cause
    # false SHORT hits on pins that haven't had their own GPIO_SET/CLEAR yet.
    print("  Pre-driving all CN5 output pins LOW ...")
    for _, pin, _, _ in CN5_PINS:
        dut_cmd(tc, f"GPIO_CLEAR {pin}", wait=0.5)
    # Drain any buffered DUT responses before starting the sweep — rapid
    # GPIO_CLEAR burst can leave stale bytes in the socket that corrupt the
    # first few tie read calls.
    tc_cmd(tc, "mux release", wait=1.5)

    all_pins = CN5_PINS + [CN5_AUDIO]

    for driven_mux, driven_pin, driven_sig, driven_conn in CN5_PINS:
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

        # Check all other pins for unexpected HIGH (short detection)
        for nbr_mux, nbr_pin, nbr_sig, nbr_conn in all_pins:
            if nbr_mux == driven_mux:
                continue  # skip self
            mv_nbr = run_mux_read(tc, *nbr_mux, label=f"{nbr_conn} while {driven_pin}=H")
            no_short = mv_nbr is None or mv_nbr < SHORT_MV_THRESHOLD
            if not no_short:
                print(f"    SHORT: {nbr_conn} {nbr_sig} reads {mv_nbr} mV "
                      f"while {driven_conn} {driven_pin} is HIGH")
            r.check(
                f"SHORT {driven_conn}→{nbr_conn} ({driven_pin}↔{nbr_pin if nbr_pin != '—' else nbr_sig})",
                no_short,
                f"{mv_nbr} mV" if mv_nbr is not None else "ERR",
            )

        # Drive LOW and verify
        dut_cmd(tc, f"GPIO_CLEAR {driven_pin}", wait=0.5)
        mv_low = run_mux_read(tc, *driven_mux, label=f"{driven_conn} released LOW")
        r.check(f"{driven_conn} {driven_pin} → LOW",
                mv_low is not None and mv_low < LOW_MV_MAX, mv_str(mv_low))


def main() -> None:
    ap = argparse.ArgumentParser(description="CN5 inter-pin short detection")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "CN5 shorts", do_flash=not args.no_flash)
            if ok:
                phase_short_detect(tc, r)
        finally:
            teardown(tc)

    r.summary()
    sys.exit(0 if r._fail == 0 else 1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""profile_dut_current.py — Progressive peripheral enable with current profiling.

Powers up the DUT, enters test mode, then enables peripherals one at a
time while monitoring VDUT1 supply current via INA219. Reports per-step
current draw and delta to show each peripheral's contribution.

Usage:
    python3 scripts/profile_dut_current.py
    python3 scripts/profile_dut_current.py --no-flash
"""
from __future__ import annotations
import argparse
import re
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole                                    # noqa: E402
from test_helpers import (Results, dut_cmd, tc_cmd,                # noqa: E402
                           phase_bring_up, teardown)

# ── INA219 constants ─────────────────────────────────────────────────────────
INA219_ADDR      = 0x40   # VDUT1
INA219_REG_CFG   = 0x00
INA219_REG_SHUNT = 0x01
INA219_REG_BUS   = 0x02
INA219_REG_CUR   = 0x04
INA219_REG_CAL   = 0x05
INA219_CUR_LSB_UA = 10    # 10 µA per LSB

_BYTES_RE = re.compile(r"bytes\]:\s*((?:[0-9A-Fa-f]{2}\s*)+)")


def _parse_i2c_bytes(resp: str) -> list[int]:
    """Extract hex bytes from an 'i2c read' response."""
    for line in resp.splitlines():
        m = _BYTES_RE.search(line)
        if m:
            return [int(b, 16) for b in m.group(1).split()]
    return []


def ina219_init(tc: TcConsole) -> None:
    """Configure INA219 at VDUT1 addr: continuous, 32-sample avg."""
    tc.cmd(f"i2c write 0x{INA219_ADDR:02x} "
           f"0x{INA219_REG_CFG:02x} 0x21 0x9F", wait=0.2)
    tc.cmd(f"i2c write 0x{INA219_ADDR:02x} "
           f"0x{INA219_REG_CAL:02x} 0xA0 0x00", wait=0.2)
    time.sleep(0.5)


def ina219_read_current_ma(tc: TcConsole, samples: int = 3) -> float | None:
    """Read VDUT1 current in mA, averaged over N samples."""
    readings: list[float] = []
    for _ in range(samples):
        resp = tc.cmd(f"i2c read 0x{INA219_ADDR:02x} "
                      f"0x{INA219_REG_CUR:02x} 2", wait=0.3)
        raw = _parse_i2c_bytes(resp)
        if len(raw) < 2:
            continue
        val = (raw[0] << 8) | raw[1]
        if val > 0x7FFF:
            val -= 0x10000
        readings.append(val * INA219_CUR_LSB_UA / 1000.0)
        time.sleep(0.1)
    if not readings:
        return None
    return sum(readings) / len(readings)


def ina219_read_voltage_mv(tc: TcConsole) -> int | None:
    """Read VDUT1 bus voltage in mV."""
    resp = tc.cmd(f"i2c read 0x{INA219_ADDR:02x} "
                  f"0x{INA219_REG_BUS:02x} 2", wait=0.3)
    raw = _parse_i2c_bytes(resp)
    if len(raw) < 2:
        return None
    return (((raw[0] << 8) | raw[1]) >> 3) * 4


# ── Peripheral sequence ──────────────────────────────────────────────────────
# Each entry: (label, list of DUT GPIO_SET commands to execute)
# Cumulative — each step adds to the previous state.

PERIPHERAL_STEPS: list[tuple[str, list[str]]] = [
    ("Baseline (MCU in test mode)",  []),
    # ── LEDs ──
    ("+ LED_R (PC7)",                ["GPIO_SET PC7 HIGH"]),
    ("+ LED_G (PB4)",                ["GPIO_SET PB4 HIGH"]),
    ("+ LED_B (PE5)",                ["GPIO_SET PE5 HIGH"]),
    # ── Display ──
    ("+ DISP_LED_ENA (PE6)",         ["GPIO_SET PE6 HIGH"]),
    ("+ LCD_DISP_ENA (PC4)",         ["GPIO_SET PC4 HIGH"]),
    ("+ LCD_SPI2_CS_H (PE13)",       ["GPIO_SET PE13 HIGH"]),
    # ── Sounder ──
    ("+ SOUNDER_gain1 (PD7)",        ["GPIO_SET PD7 HIGH"]),
    ("+ SOUNDER_gain2 (PD9)",        ["GPIO_SET PD9 HIGH"]),
    ("+ SOUNDER_volume (PB3)",       ["GPIO_SET PB3 HIGH"]),
    # ── Flashlight & sensor ──
    ("+ FLASHLIGHT_ENA (PA15)",      ["GPIO_SET PA15 HIGH"]),
    ("+ 2611_SENSOR_ENA (PC8)",      ["GPIO_SET PC8 HIGH"]),
    # ── VIN branch enables ──
    ("+ VIN#1_ENA (PD10)",           ["GPIO_SET PD10 HIGH"]),
    ("+ VIN#2_ENA (PD11)",           ["GPIO_SET PD11 HIGH"]),
    ("+ VIN#3_ENA (PB13)",           ["GPIO_SET PB13 HIGH"]),
    ("+ VIN#4_ENA (PD13)",           ["GPIO_SET PD13 HIGH"]),
    ("+ VIN#5_ENA (PD14)",           ["GPIO_SET PD14 HIGH"]),
    ("+ VIN#6_ENA (PD2)",            ["GPIO_SET PD2 HIGH"]),
    # ── BT/GPS I2C ──
    ("+ BT/GPS_I2C1_SDA (PB9)",     ["GPIO_SET PB9 HIGH"]),
    ("+ BT/GPS_I2C1_SCL (PB8)",     ["GPIO_SET PB8 HIGH"]),
    # ── TC_MODE / SS_ENA ──
    ("+ TC_MODE (PE9)",              ["GPIO_SET PE9 HIGH"]),
    ("+ SS_ENA_A (PA9)",             ["GPIO_SET PA9 HIGH"]),
    ("+ SS_ENA_B (PD15)",            ["GPIO_SET PD15 HIGH"]),
]

SETTLE_S = 0.5   # seconds to wait after enabling before measuring


def phase_profile(tc: TcConsole) -> None:
    """Progressive peripheral enable with current measurement."""
    print("\n── DUT Peripheral Current Profile ──────────────────────────────────────")

    ina219_init(tc)
    v_mv = ina219_read_voltage_mv(tc)
    print(f"  VDUT1 bus voltage: {v_mv} mV" if v_mv else "  VDUT1 voltage: ERR")

    print(f"\n  {'#':>3}  {'Peripheral':<38}  {'I (mA)':>8}  {'Δ (mA)':>8}  {'Total Δ':>8}")
    print(f"  {'─'*3}  {'─'*38}  {'─'*8}  {'─'*8}  {'─'*8}")

    results: list[tuple[str, float, float]] = []
    prev_ma = 0.0
    base_ma = 0.0

    for i, (label, cmds) in enumerate(PERIPHERAL_STEPS):
        for cmd in cmds:
            dut_cmd(tc, cmd, wait=0.3)

        if cmds:
            time.sleep(SETTLE_S)

        cur_ma = ina219_read_current_ma(tc)
        if cur_ma is None:
            print(f"  {i:>3}  {label:<38}  {'ERR':>8}  {'':>8}  {'':>8}")
            continue

        if i == 0:
            base_ma = cur_ma
            prev_ma = cur_ma
            delta = 0.0
            total_delta = 0.0
        else:
            delta = cur_ma - prev_ma
            total_delta = cur_ma - base_ma
            prev_ma = cur_ma

        results.append((label, cur_ma, delta))
        print(f"  {i:>3}  {label:<38}  {cur_ma:>8.1f}  {delta:>+8.1f}  {total_delta:>+8.1f}")

    # ── Summary ──
    if results:
        final_ma = results[-1][1]
        print(f"\n  {'─'*71}")
        print(f"  Baseline:  {base_ma:.1f} mA")
        print(f"  Final:     {final_ma:.1f} mA  (all peripherals enabled)")
        print(f"  Increase:  {final_ma - base_ma:+.1f} mA")

        # Top contributors
        deltas = [(label, delta) for label, _, delta in results if delta > 0.5]
        if deltas:
            deltas.sort(key=lambda x: x[1], reverse=True)
            print(f"\n  Top contributors:")
            for label, delta in deltas[:8]:
                print(f"    {delta:>+6.1f} mA  {label}")


def main() -> None:
    ap = argparse.ArgumentParser(description="DUT peripheral current profiling")
    ap.add_argument("--no-flash", action="store_true",
                    help="Skip SWD flash — DUT already running pfw")
    args = ap.parse_args()

    r = Results()
    with TcConsole() as tc:
        try:
            ok = phase_bring_up(tc, r, "current profile", do_flash=not args.no_flash)
            if ok:
                phase_profile(tc)
        finally:
            teardown(tc)

    r.summary()


if __name__ == "__main__":
    main()

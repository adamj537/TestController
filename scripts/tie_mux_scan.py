#!/usr/bin/env python3
"""Scan all 64 TIE mux channels and flag floating/unconnected signals.

Runs two detection passes and unions the results:

  Pass 1 — Unpowered dual scan
    Two back-to-back 'selftest mux' runs with DUT unpowered.
    Floating inputs are unstable (ADC capacitor charges/discharges
    from whatever was previously on the bus).

  Pass 2 — Charge-and-cut scan
    Power VDUT1 + assert PB-A briefly so DUT drives outputs.
    Cut power, wait 300 ms, then run dual scan.
    Connected traces hold DUT-driven charge; unconnected pogos
    never charged — they float between two reads.

A channel is FLOATING in the union if EITHER pass flags it.
A channel confirmed FLOATING by both passes is HIGH CONFIDENCE.

Usage:
    python3 scripts/tie_mux_scan.py
"""
import socket
import sys
import time
import re
import os
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT

# ── Signal map: (mux, ch) → (signal_name, connector)
SIGNAL_MAP: dict[tuple[int, int], tuple[str, str]] = {
    # MUX0 — CN5 Breakout
    (0,  0): ("DISP_LED_ENA / PE6",          "CN5-14"),
    (0,  3): ("SOUNDER_audio",                "CN5-10"),
    (0,  4): ("3V_Branch#5",                  "CN5-13"),
    (0,  5): ("LED_G / PB4",                  "CN5-9"),
    (0,  6): ("LCD_SPI2_SCK / PD1",           "CN5-16"),
    (0,  7): ("LED_R / PC7",                  "CN5-8"),
    (0,  8): ("MAIN_I2C3_SCL / PC0",          "CN5-15"),
    (0,  9): ("SOUNDER_volume / PB3",         "CN5-7"),
    (0, 10): ("LCD_DISP_ENA / PC4",           "CN5-26"),
    (0, 11): ("PB-A/PWR-ON",                  "CN5-25"),
    (0, 12): ("LCD_SPI2_CS_H / PE13",         "CN5-23"),
    (0, 13): ("5V_Branch#5",                  "CN5-22"),
    (0, 14): ("BUTTON_C / PA12",              "CN5-21"),
    # MUX1 — CN5, CN1, CN3, CN4, CN8
    (1,  0): ("MAIN_I2C3_SDA / PC1",         "CN5-20"),
    (1,  1): ("BUTTON_B / PA11",              "CN5-19"),
    (1,  2): ("VBranch#2_Sensor",             "CN3-2"),
    (1,  3): ("TC_enable / PC9",              "CN3-3"),
    (1,  4): ("TC_SCL / PB10",               "CN3-1"),
    (1,  6): ("Pump_Hi",                      "CN4-1"),
    (1,  7): ("VIN#7",                        "CN1-3"),
    (1,  8): ("Pump_Lo",                      "CN4-2"),
    (1,  9): ("VIN_G3",                       "CN1-1"),
    (1, 10): ("VIN#3",                        "CN8-2"),
    (1, 11): ("SDA_Rx",                       "CN8-3"),
    (1, 12): ("#3_3V_Volt_Mon",               "CN8-6"),
    (1, 13): ("SCL_Tx",                       "CN8-5"),
    (1, 14): ("#3_CP_Volt_Mon",               "CN8-8"),
    (1, 15): ("#3_3V_Curr_Mon",               "CN8-7"),
    # MUX2 — CN8, CN7, CN5
    (2,  0): ("TC_MODE / PE9",               "CN8-11"),
    (2,  1): ("VIN#3_ENA / PB13",            "CN8-9"),
    (2,  2): ("SS_ENA_A / PA9",              "CN8-12"),
    (2,  3): ("SS_ENA_B / PD15",             "CN8-10"),
    (2,  4): ("BT_USART2_TX / PD5",          "CN7-12"),
    (2,  5): ("BT_USART2_CTS / PD3",         "CN7-14"),
    (2,  6): ("BT_SPI3_CS/GPIO3 / PE3",      "CN7-10"),
    (2,  7): ("BT_USART2_RX / PD6",          "CN7-11"),
    (2,  8): ("BT_GPIO2 / PE8",              "CN7-8"),
    (2,  9): ("BT_GPIO1 / PE7",              "CN7-9"),
    (2, 10): ("BT/GPS_I2C1_SCL / PB8",       "CN7-6"),
    (2, 11): ("GPS_SPI3_CS / PD0",           "CN7-7"),
    (2, 12): ("BT/GPS_I2C1_SDA / PB9",       "CN7-5"),
    (2, 13): ("Vradio Supply",               "CN7-1/2"),
    (2, 14): ("SOUNDER_gain2 / PD9",         "CN5-6"),
    # MUX3 — CN5, CN7, CN2
    (3,  0): ("LCD_EXTCOMIN / PD12",         "CN5-18"),
    (3,  1): ("SOUNDER_gain1 / PD7",         "CN5-5"),
    (3,  2): ("LCD_SPI2_MOSI / PB15",        "CN5-17"),
    (3,  3): ("LED_B / PE5",                 "CN5-4"),
    (3,  4): ("VBranch#2",                   "CN5-1"),
    (3,  5): ("BT_USART2_RTS / PD4",         "CN7-13"),
    (3,  6): ("VBranch#1",                   "CN2-1"),
    (3,  7): ("BT/GPS_SPI3_MOSI / PE14",     "CN7-16"),
    (3,  8): ("VREF_DIV",                    "CN2-2"),   # analog ~1500–2560 mV
    (3,  9): ("GPS_USART1_TX / PB6",         "CN7-17"),
    (3, 10): ("Flashlight_ENA",              "CN2-3"),
    (3, 11): ("SPI3_SCK/GPS_GPIO1 / PC10",   "CN7-18"),
    (3, 12): ("2611_Vout (LEL sensor)",      "CN2-4"),
    (3, 13): ("GPS_USART1_RX / PB7",         "CN7-15"),
    (3, 14): ("GPS_GPIO2 / PB5",             "CN7-19"),
    (3, 15): ("BT/GPS_SPI3_MISO / PE15",     "CN7-20"),
}

ANALOG_KEYS = {(3, 8)}   # VREF_DIV — expected mid-rail
UNUSED_KEYS = {
    (0,  1),  # MUX0_UNUSED_2
    (0,  2),  # MUX0_UNUSED_1
    (0, 15),  # NC
    (1,  5),  # TC_SDA — dead, HW-011 ESD damage
    (1,  9),  # VIN_G3 — trace cut intentionally (backfeed protection)
    (2, 15),  # NC
}

LOW_MV   = 100
HIGH_MV  = 2700
DELTA_MV = 80


# ── TCP helpers ───────────────────────────────────────────────────────────────

def send_cmd(sock: socket.socket, cmd: str, wait: float = 25.0) -> str:
    sock.sendall((cmd + "\n").encode())
    out: list[str] = []
    deadline = time.time() + wait
    sock.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except socket.timeout:
            continue
    return "".join(out)


def parse_scan(raw: str) -> dict[tuple[int, int], Optional[int]]:
    row_re = re.compile(r'^\s*(\d)\s+(\d+)\s+\d\s+\d\s+\d\s+\d\s+(ERR|[-\d]+)')
    result: dict[tuple[int, int], Optional[int]] = {}
    for line in raw.splitlines():
        m = row_re.match(line)
        if m:
            key = (int(m.group(1)), int(m.group(2)))
            result[key] = None if m.group(3) == "ERR" else int(m.group(3))
    return result


def run_scan(sock: socket.socket, label: str) -> dict[tuple[int, int], Optional[int]]:
    print(f"    {label} ...")
    raw = send_cmd(sock, "selftest mux", wait=25.0)
    result = parse_scan(raw)
    if not result:
        print(f"  ERROR: no data returned. Raw ({len(raw)} chars):\n{raw[:400]}")
        sys.exit(1)
    return result


def is_floating(mv1: int, mv2: int, key: tuple[int, int]) -> bool:
    if key in ANALOG_KEYS:
        return False
    delta = abs(mv1 - mv2)
    avg   = (mv1 + mv2) // 2
    return delta > DELTA_MV or (LOW_MV <= avg <= HIGH_MV)


# ── Pass 1: unpowered dual scan ───────────────────────────────────────────────

def pass_unpowered(sock: socket.socket) -> set[tuple[int, int]]:
    print("\n── Pass 1: Unpowered dual scan ──────────────────────────────────")
    s1 = run_scan(sock, "Scan A")
    time.sleep(1)
    s2 = run_scan(sock, "Scan B")
    floats: set[tuple[int, int]] = set()
    for key in s1:
        if key in UNUSED_KEYS:
            continue
        mv1, mv2 = s1.get(key), s2.get(key)
        if mv1 is not None and mv2 is not None and is_floating(mv1, mv2, key):
            floats.add(key)
    print(f"  → {len(floats)} floating channels detected")
    return floats


# ── Pass 2: charge-and-cut scan ───────────────────────────────────────────────

def pass_charge_and_cut(sock: socket.socket) -> set[tuple[int, int]]:
    print("\n── Pass 2: Charge-and-cut scan ──────────────────────────────────")
    print("  Stopping DUT detect task ...")
    send_cmd(sock, "dut stop", wait=1.0)
    time.sleep(2.0)
    print("  Enabling VDUT1 (vdac_voltage 1 3300) + PB-A ...")
    send_cmd(sock, "vdac_voltage 1 3300", wait=3.0)
    send_cmd(sock, "mux select 0 0", wait=0.5)
    print("  Waiting 4 s for DUT to boot and drive outputs ...")
    time.sleep(4.0)
    print("  Cutting power — trace charge holds briefly ...")
    send_cmd(sock, "mux release", wait=0.5)
    send_cmd(sock, "vdac off",    wait=1.0)   # vdac_duty 1 0 rejected when DUT seated
    time.sleep(0.3)
    s1 = run_scan(sock, "Scan A (300 ms after power cut)")
    time.sleep(1)
    s2 = run_scan(sock, "Scan B")
    print("  Restoring DUT detect task ...")
    send_cmd(sock, "dut start", wait=0.5)
    floats: set[tuple[int, int]] = set()
    for key in s1:
        if key in UNUSED_KEYS:
            continue
        mv1, mv2 = s1.get(key), s2.get(key)
        if mv1 is not None and mv2 is not None and is_floating(mv1, mv2, key):
            floats.add(key)
    print(f"  → {len(floats)} floating channels detected")
    return floats


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.settimeout(1.0)
    time.sleep(0.4)
    try:
        s.recv(4096)
    except Exception:
        pass

    floats_p1 = pass_unpowered(s)
    floats_p2 = pass_charge_and_cut(s)
    s.close()

    union     = floats_p1 | floats_p2
    both      = floats_p1 & floats_p2
    p1_only   = floats_p1 - floats_p2
    p2_only   = floats_p2 - floats_p1

    print(f"\n{'─'*72}")
    print(f"Pass 1 (unpowered)   : {len(floats_p1):2d} floating")
    print(f"Pass 2 (charge+cut)  : {len(floats_p2):2d} floating")
    print(f"Union (either pass)  : {len(union):2d} floating")
    print(f"Both passes agreed   : {len(both):2d} (high confidence)")

    print(f"\n{'MUX':>3} {'CH':>2}  {'CONFIDENCE':<12}  {'SIGNAL':<40}  CONNECTOR")
    print("─" * 80)

    for key in sorted(union):
        mux_idx, ch = key
        sig, conn = SIGNAL_MAP.get(key, ("—", "—"))
        if key in both:
            conf = "BOTH ●●"
        elif key in p1_only:
            conf = "unpowered ○"
        else:
            conf = "powered ○"
        print(f"  {mux_idx}  {ch:2d}  {conf:<12}  {sig:<40}  {conn}")


if __name__ == "__main__":
    main()

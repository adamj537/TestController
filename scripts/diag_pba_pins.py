#!/usr/bin/env python3
"""Targeted PB-A pin diagnostic — checks every link in the chain.

1. Vshunt sanity check (INA219 reg 0x01, I = Vshunt / 0.1 Ohm)
2. DAC MUX address pin states (GPIO17/18/21 — must be OUTPUT + LOW for ch0)
3. GPIO0 (SIG) state — must be OUTPUT + LOW for PB-A assert
4. INA219 bus voltage + current register cross-check
"""
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT


def send_cmd(s: socket.socket, cmd: str, wait: float = 1.0) -> str:
    s.sendall((cmd + "\n").encode())
    time.sleep(wait)
    data = b""
    s.settimeout(2.0)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data.decode(errors="replace")


def parse_i2c_read(resp: str) -> int:
    """Return raw 16-bit value from 'i2c read' response, or -1."""
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def signed16(raw: int) -> int:
    return raw - 0x10000 if raw > 0x7FFF else raw


def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(3.0)
    try:
        s.recv(4096)
    except socket.timeout:
        pass

    # ── Step 0: Stop DUT detect ──────────────────────────────────────────
    print("\n[0] Stopping DUT detect ...")
    resp = send_cmd(s, "dut stop", wait=0.5)
    print(f"    {[l for l in resp.splitlines() if 'stop' in l.lower() or 'DUT' in l]}")
    print("    Waiting 3s for task exit ...")
    time.sleep(3.0)

    # ── Step 1: Enable VDUT1 ─────────────────────────────────────────────
    print("\n[1] Enabling VDUT1 (vdac_voltage 1 3300) ...")
    resp = send_cmd(s, "vdac_voltage 1 3300", wait=4.0)
    for line in resp.splitlines():
        if "VDUT" in line or "mV" in line:
            print(f"    {line.strip()}")

    # ── Step 2: Assert PB-A via firmware mux ─────────────────────────────
    print("\n[2] Asserting PB-A (mux select 0 0) ...")
    resp = send_cmd(s, "mux select 0 0", wait=1.0)
    for line in resp.splitlines():
        if "mux" in line.lower() or "U8" in line:
            print(f"    {line.strip()}")
    time.sleep(0.5)

    # ── Step 3: Read all DAC MUX pin states ──────────────────────────────
    print("\n[3] DAC MUX pin state check:")
    pins = {
        "GPIO0 (SIG)": 0,
        "GPIO17 (A0)": 17,
        "GPIO18 (A1)": 18,
        "GPIO21 (A2)": 21,
    }
    for label, pin in pins.items():
        resp = send_cmd(s, f"gpio get {pin}", wait=0.5)
        level = "?"
        for line in resp.splitlines():
            if f"GPIO{pin}" in line and "=" in line:
                level = line.split("=")[-1].strip()
        print(f"    {label}: level={level}  (expect 0 for PB-A ch0)")

    # ── Step 4: INA219 Vshunt sanity check ───────────────────────────────
    print("\n[4] INA219 Vshunt sanity check:")
    # Write config + cal first
    send_cmd(s, "i2c write 0x40 0x00 0x21 0x9F", wait=0.3)
    send_cmd(s, "i2c write 0x40 0x05 0xA0 0x00", wait=0.3)
    time.sleep(0.5)

    # Read Vshunt (reg 0x01) — LSB = 10 µV
    resp = send_cmd(s, "i2c read 0x40 0x01 2", wait=0.5)
    vshunt_raw = parse_i2c_read(resp)
    vshunt_uv = signed16(vshunt_raw) * 10 if vshunt_raw >= 0 else -1
    vshunt_mv = vshunt_uv / 1000.0

    # Read Vbus (reg 0x02) — bits[15:3], LSB = 4 mV
    resp = send_cmd(s, "i2c read 0x40 0x02 2", wait=0.5)
    vbus_raw = parse_i2c_read(resp)
    vbus_mv = (vbus_raw >> 3) * 4 if vbus_raw >= 0 else -1

    # Read Current (reg 0x04) — uses CAL register, LSB = 10 µA
    resp = send_cmd(s, "i2c read 0x40 0x04 2", wait=0.5)
    cur_raw = parse_i2c_read(resp)
    cur_ua = signed16(cur_raw) * 10 if cur_raw >= 0 else -1

    # Read CAL register (reg 0x05)
    resp = send_cmd(s, "i2c read 0x40 0x05 2", wait=0.5)
    cal_raw = parse_i2c_read(resp)

    # Compute expected current from Vshunt
    rshunt = 0.1  # 0.1 Ohm
    i_from_vshunt_ma = (vshunt_uv / 1000.0) / rshunt if vshunt_uv >= 0 else -1

    print(f"    CAL register:  0x{cal_raw:04X}  (expect 0xA000)")
    print(f"    Vshunt:        {vshunt_raw} raw → {vshunt_mv:.3f} mV ({vshunt_uv} µV)")
    print(f"    Vbus:          {vbus_raw} raw → {vbus_mv} mV")
    print(f"    Current reg:   {cur_raw} raw → {cur_ua} µA ({cur_ua/1000:.1f} mA)")
    print(f"    I from Vshunt: {vshunt_mv:.3f} mV / {rshunt} Ω = {i_from_vshunt_ma:.1f} mA")
    print()
    if abs(cur_ua/1000.0 - i_from_vshunt_ma) > 5:
        print(f"    *** MISMATCH: Current reg ({cur_ua/1000:.1f} mA) ≠ Vshunt calc ({i_from_vshunt_ma:.1f} mA)")
    else:
        print(f"    Current reading CONSISTENT — INA219 is working correctly")

    if i_from_vshunt_ma < 5:
        print(f"    *** DUT NOT drawing current — PB-A signal not reaching DUT")
        print(f"        Check: pogo contact, MUX output, DUT PB-A circuit")
    else:
        print(f"    DUT drawing {i_from_vshunt_ma:.1f} mA — digital rail should be up")

    # ── Step 5: Cleanup ──────────────────────────────────────────────────
    print("\n[5] Cleanup ...")
    send_cmd(s, "mux release", wait=0.5)
    send_cmd(s, "vdac off", wait=0.5)
    send_cmd(s, "dut start", wait=0.5)
    print("    Done — MUX released, VDUT off, DUT detect restarted")

    s.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Clean PB-A diagnostic — NO gpio get (it destroys OUTPUT config).

Reads INA219 Vshunt IMMEDIATELY after PB-A assert, before any pin probing.
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


def parse_i2c(resp: str) -> int:
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def signed16(raw: int) -> int:
    return raw - 0x10000 if raw > 0x7FFF else raw


def read_ina219(s: socket.socket) -> tuple[int, int, float]:
    """Returns (vbus_mv, cur_ua, vshunt_computed_ma)."""
    # Write config + cal
    send_cmd(s, "i2c write 0x40 0x00 0x21 0x9F", wait=0.2)
    send_cmd(s, "i2c write 0x40 0x05 0xA0 0x00", wait=0.2)
    time.sleep(0.5)
    # Vshunt
    vshunt_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x01 2", wait=0.3))
    vshunt_uv = signed16(vshunt_raw) * 10 if vshunt_raw >= 0 else 0
    # Vbus
    vbus_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x02 2", wait=0.3))
    vbus_mv = (vbus_raw >> 3) * 4 if vbus_raw >= 0 else -1
    # Current
    cur_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x04 2", wait=0.3))
    cur_ua = signed16(cur_raw) * 10 if cur_raw >= 0 else 0
    i_calc = vshunt_uv / 1000.0 / 0.1  # mA
    return vbus_mv, cur_ua, i_calc


def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(3.0)
    try:
        s.recv(4096)
    except socket.timeout:
        pass

    # 1. Stop DUT detect + wait
    print("\n[1] dut stop ...")
    send_cmd(s, "dut stop", wait=0.5)
    time.sleep(3.0)
    print("    Waited 3s")

    # 2. VDUT off first (clean state)
    print("\n[2] vdac off (clean baseline) ...")
    send_cmd(s, "vdac off", wait=1.0)

    # 3. Baseline INA219 (VDUT off, no PB-A)
    print("\n[3] Baseline INA219 (VDUT off):")
    vbus, cur, icalc = read_ina219(s)
    print(f"    Vbus={vbus} mV  Icur={cur/1000:.1f} mA  Icalc={icalc:.1f} mA")

    # 4. Enable VDUT1 at 3300 mV
    print("\n[4] vdac_voltage 1 3300 ...")
    resp = send_cmd(s, "vdac_voltage 1 3300", wait=4.0)
    for line in resp.splitlines():
        if "VDUT" in line:
            print(f"    {line.strip()}")

    # 5. INA219 with VDUT on, NO PB-A
    print("\n[5] INA219 with VDUT on, PB-A NOT asserted:")
    vbus, cur, icalc = read_ina219(s)
    print(f"    Vbus={vbus} mV  Icur={cur/1000:.1f} mA  Icalc={icalc:.1f} mA")

    # 6. Assert PB-A via firmware mux
    print("\n[6] mux select 0 0 (PB-A assert) ...")
    resp = send_cmd(s, "mux select 0 0", wait=0.5)
    for line in resp.splitlines():
        if "mux" in line.lower() or "U8" in line:
            print(f"    {line.strip()}")

    # 7. Wait for digital rail, then INA219 — NO gpio get anywhere!
    print("\n[7] Waiting 1s for digital rail ...")
    time.sleep(1.0)

    print("\n[8] INA219 with VDUT on + PB-A asserted (no GPIO touched!):")
    vbus, cur, icalc = read_ina219(s)
    print(f"    Vbus={vbus} mV  Icur={cur/1000:.1f} mA  Icalc={icalc:.1f} mA")

    if icalc > 20:
        print(f"    ✓ DUT drawing {icalc:.0f} mA — digital rail latched!")
    else:
        print(f"    ✗ Only {icalc:.1f} mA — DUT not powered")

    # 9. Try waiting longer and read again
    print("\n[9] Waiting 3s more ...")
    time.sleep(3.0)
    vbus2, cur2, icalc2 = read_ina219(s)
    print(f"    Vbus={vbus2} mV  Icur={cur2/1000:.1f} mA  Icalc={icalc2:.1f} mA")

    # 10. Cleanup
    print("\n[10] Cleanup ...")
    send_cmd(s, "mux release", wait=0.5)
    send_cmd(s, "vdac off", wait=0.5)
    send_cmd(s, "dut start", wait=0.5)
    print("     Done")
    s.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Power DUT (PFW just flashed), read SWD IDCODE, and measure INA219 current."""
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

INA219_CURRENT_LSB_UA = 10


def send_cmd(s: socket.socket, cmd: str, wait: float = 0.5) -> str:
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


def read_ina(s: socket.socket, label: str) -> tuple[int, int]:
    send_cmd(s, "i2c write 0x40 0x00 0x21 0x9F", 0.3)
    send_cmd(s, "i2c write 0x40 0x05 0xA0 0x00", 0.3)
    time.sleep(0.5)
    bus_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x02 2", 0.5))
    cur_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x04 2", 0.5))
    sht_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x01 2", 0.5))
    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    sht_uv = (sht_raw - 0x10000 if sht_raw > 0x7FFF else sht_raw) * 10 if sht_raw >= 0 else -1
    print(f"  {label}:")
    print(f"    Bus voltage : {bus_mv} mV")
    print(f"    Shunt voltage: {sht_uv} uV")
    print(f"    Current     : {cur_ua / 1000:.1f} mA")
    return bus_mv, cur_ua


def main() -> int:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(3.0)
    try:
        s.recv(4096)
    except socket.timeout:
        pass

    # 1. Stop DUT detect
    print("\n[1] Stop DUT detect")
    send_cmd(s, "dut stop", 1)
    time.sleep(2)

    # 2. Power off — ensure clean state
    print("[2] Power off (3s discharge)")
    send_cmd(s, "gpio mode 0 out")
    send_cmd(s, "gpio mode 17 out")
    send_cmd(s, "gpio mode 18 out")
    send_cmd(s, "gpio mode 21 out")
    send_cmd(s, "gpio set 0 1")   # SIG HIGH (PBA released)
    send_cmd(s, "vdac off", 1)
    time.sleep(3)

    # 3. Power on + PBA via raw GPIO
    print("[3] Power on VDUT1 + assert PBA (raw GPIO)")
    send_cmd(s, "vdac_duty 1 80", 1)
    time.sleep(0.3)
    send_cmd(s, "gpio set 17 0")  # A0
    send_cmd(s, "gpio set 18 0")  # A1
    send_cmd(s, "gpio set 21 0")  # A2
    send_cmd(s, "gpio set 0 1")   # SIG HIGH first
    time.sleep(0.1)
    send_cmd(s, "gpio set 0 0")   # SIG LOW — PBA asserted
    print("  PB-A asserted, waiting 1.5s for PFW boot ...")
    time.sleep(1.5)

    # 4. INA219 read — PFW current
    print("\n[4] INA219 — PFW running")
    read_ina(s, "PFW")

    # 5. SWD probe (nopwrcycle — already powered)
    print("\n[5] SWD IDCODE (nopwrcycle)")
    resp = send_cmd(s, "swd flash local nopwrcycle --target pfw", 3)
    # Actually just want IDCODE — use swd probe? No, probe does its own power cycle.
    # Let's just parse the IDCODE from whatever output we get
    # Better: send a quick connect-only. But there's no such command.
    # We'll read the full flash output — it prints IDCODE before flashing.
    # Actually that would re-flash. Let's just do a manual SWD connect sequence.
    # Simplest: just read the swd probe output but it power-cycles...
    # Use nopwrcycle flash which prints IDCODE then flashes (same PFW, harmless)

    buf = b""
    start = time.time()
    s.settimeout(30)
    while time.time() - start < 30:
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            text = chunk.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            if b"[PASS]" in buf or b"[FAIL]" in buf:
                break
        except socket.timeout:
            continue

    # 6. INA219 read again after re-flash (still PFW)
    print("\n\n[6] INA219 — PFW post-reflash")
    time.sleep(0.5)
    read_ina(s, "PFW (post-reflash)")

    # 7. Cleanup
    print("\n[7] Cleanup")
    send_cmd(s, "gpio set 0 1")
    send_cmd(s, "vdac off", 1)
    send_cmd(s, "dut start", 1)
    s.close()

    print("\n========================================")
    print("Compare with product FW from last run:")
    print("  Product FW: ~270 mA @ ~3880 mV")
    print("========================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())

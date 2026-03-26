#!/usr/bin/env python3
"""Read INA219 #0 and #1 bus voltage, current, shunt on a persistent TCP connection.

Usage:
    python3 scripts/read_ina219.py [--host 192.168.50.30]
"""

import argparse
import socket
import sys
import time

HOST = "192.168.50.30"
PORT = 4242

INA219_CURRENT_LSB_UA = 10


def cmd(sock: socket.socket, command: str, wait: float = 3.0) -> str:
    sock.sendall((command + "\n").encode())
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


def parse_i2c_read(resp: str) -> int:
    """Extract 16-bit value from 'bytes]: XX YY' response."""
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def read_ina(sock: socket.socket, addr: int, label: str) -> None:
    # Init: config + cal
    cmd(sock, f"i2c write 0x{addr:02x} 0x00 0x21 0x9F", wait=2)
    cmd(sock, f"i2c write 0x{addr:02x} 0x05 0xA0 0x00", wait=2)
    time.sleep(0.5)

    bus_raw = parse_i2c_read(cmd(sock, f"i2c read 0x{addr:02x} 0x02 2", wait=2))
    cur_raw = parse_i2c_read(cmd(sock, f"i2c read 0x{addr:02x} 0x04 2", wait=2))
    sht_raw = parse_i2c_read(cmd(sock, f"i2c read 0x{addr:02x} 0x01 2", wait=2))

    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    sht_uv = sht_raw * 10 if sht_raw >= 0 else -1

    print(f"  {label} (0x{addr:02X}):")
    print(f"    Bus voltage : {bus_mv} mV")
    print(f"    Current     : {cur_ua} uA  ({cur_ua / 1000:.1f} mA)")
    print(f"    Shunt       : {sht_uv} uV")


def main() -> int:
    parser = argparse.ArgumentParser(description="Read INA219 current sensors")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((args.host, PORT))
    except Exception as e:
        print(f"FAIL: {e}")
        return 1
    time.sleep(0.3)
    sock.settimeout(0.3)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass

    print("INA219 readings:")
    read_ina(sock, 0x40, "INA219 #0 (VDUT1)")
    read_ina(sock, 0x41, "INA219 #1 (VDUT2)")

    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

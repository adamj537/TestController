#!/usr/bin/env python3
"""Read dut_fw and prod_fw partition headers from TC flash via CLI console.

Uses the `flash read` console command (serial or TCP) to read 32-byte
partition headers.  Does not require esptool or cause hard resets.

Usage:
    check_stored_fw.py [--serial /dev/ttyACM0]
    check_stored_fw.py [--tcp 192.168.50.30]
"""
import argparse
import os
import re
import socket
import struct
import sys
import time

try:
    import serial as pyserial
except ImportError:
    pyserial = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Partition layout — must match partition table
DUT_FW_OFFSET = 0x620000
PROD_FW_OFFSET = 0x6C0000
HEADER_SIZE = 32
DUT_FW_PART_MAGIC = 0xD07F0001


def send_serial(port: str, cmd: str, timeout: float = 3.0) -> str:
    if pyserial is None:
        print("pyserial required: pip install pyserial", file=sys.stderr)
        sys.exit(1)
    with pyserial.Serial(port, 115200, timeout=timeout) as s:
        s.read(s.in_waiting or 1)
        time.sleep(0.1)
        s.write(f"{cmd}\r\n".encode())
        time.sleep(timeout)
        return s.read(4096).decode("utf-8", errors="replace")


def send_tcp(host: str, cmd: str, timeout: float = 3.0) -> str:
    s = socket.socket()
    s.settimeout(timeout)
    s.connect((host, 4242))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while s.recv(4096):
            pass
    except socket.timeout:
        pass
    s.sendall(f"{cmd}\n".encode())
    time.sleep(timeout)
    buf = b""
    s.settimeout(0.5)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    s.close()
    return buf.decode("utf-8", errors="replace")


def parse_hex_dump(resp: str) -> bytes:
    """Extract bytes from hex dump output like '  00620000: 01 00 7f d0 ...'."""
    raw = bytearray()
    for line in resp.splitlines():
        # Match lines starting with hex address
        m = re.match(r'\s*[0-9a-fA-F]{8}:\s+((?:[0-9a-fA-F]{2}\s+)+)', line)
        if m:
            hex_str = m.group(1).strip()
            for byte_str in hex_str.split():
                raw.append(int(byte_str, 16))
    return bytes(raw)


def parse_header(data: bytes, name: str) -> None:
    if len(data) < HEADER_SIZE:
        print(f"  {name}: read failed (got {len(data)} bytes, need {HEADER_SIZE})")
        return

    magic, size = struct.unpack_from("<II", data, 0)
    version_bytes = data[8:24]
    version = version_bytes.split(b"\x00")[0].decode("ascii", errors="replace")

    if magic != DUT_FW_PART_MAGIC:
        print(f"  {name}: EMPTY (magic=0x{magic:08X}, expected 0x{DUT_FW_PART_MAGIC:08X})")
        return

    print(f"  {name}: VALID")
    print(f"    size:    {size:,} bytes ({size / 1024:.1f} KB)")
    print(f"    version: {version if version else '(not set)'}")


def main() -> int:
    p = argparse.ArgumentParser(description="Check stored DUT firmware on TC flash")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--serial", default="/dev/ttyACM0",
                   help="Serial port (default: /dev/ttyACM0)")
    g.add_argument("--tcp", default="", help="TC IP for TCP console")
    args = p.parse_args()

    send = (lambda cmd: send_tcp(args.tcp, cmd)) if args.tcp else \
           (lambda cmd: send_serial(args.serial, cmd))

    transport = f"TCP {args.tcp}:4242" if args.tcp else f"serial {args.serial}"
    print(f"Reading partition headers via {transport} ...\n")

    dut_resp = send(f"flash read 0x{DUT_FW_OFFSET:X} {HEADER_SIZE}")
    prod_resp = send(f"flash read 0x{PROD_FW_OFFSET:X} {HEADER_SIZE}")

    dut_data = parse_hex_dump(dut_resp)
    prod_data = parse_hex_dump(prod_resp)

    print("Stored firmware partitions:")
    parse_header(dut_data, "dut_fw  (PFW)")
    parse_header(prod_data, "prod_fw (Product)")

    return 0


if __name__ == "__main__":
    sys.exit(main())

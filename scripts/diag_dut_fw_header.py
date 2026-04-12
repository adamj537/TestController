#!/usr/bin/env python3
"""diag_dut_fw_header.py — Read dut_fw partition header to check stored version.

Uses `flash partitions` to find the dut_fw offset, then `flash read` to dump
the 32-byte dut_fw_hdr_t header:
    uint32_t magic;        /* 0xD07F0001 */
    uint32_t size;         /* firmware size in bytes */
    char     version[16];  /* null-terminated semver string */
    uint8_t  _pad[8];      /* reserved */

Usage:
    python3 scripts/diag_dut_fw_header.py [--host 192.168.50.21]
"""

import argparse
import re
import socket
import struct
import sys
import time

from tc_config import HOST, PORT

DUT_FW_MAGIC = 0xD07F0001


class TC:
    def __init__(self, host: str = HOST):
        self.host = host
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(10)
        self.sock.connect((self.host, PORT))
        time.sleep(0.3)
        self._drain()

    def _drain(self) -> None:
        self.sock.settimeout(0.3)
        try:
            while self.sock.recv(4096):
                pass
        except socket.timeout:
            pass
        self.sock.settimeout(10)

    def cmd(self, command: str, wait: float = 5.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + wait
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if "g3-tc|" in chunk and ">" in chunk:
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


def parse_partition_offset(resp: str, label: str) -> int | None:
    """Extract partition offset from 'flash partitions' output."""
    for line in resp.splitlines():
        parts = line.split()
        if parts and parts[0] == label:
            for p in parts:
                if p.startswith("0x") and len(p) == 10:
                    return int(p, 16)
    return None


def parse_hex_dump(resp: str) -> bytes:
    """Extract raw bytes from 'flash read' hex dump output."""
    raw = bytearray()
    for line in resp.splitlines():
        # Match lines like "  00abcdef: 01 02 03 ... "
        m = re.match(r'\s+[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)', line)
        if m:
            hex_str = m.group(1).strip()
            for b in hex_str.split():
                raw.append(int(b, 16))
    return bytes(raw)


def main() -> int:
    parser = argparse.ArgumentParser(description="Read dut_fw partition header")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    tc = TC(args.host)
    print(f"Connecting to {args.host}:{PORT}...")
    try:
        tc.connect()
    except Exception as e:
        print(f"FAIL: {e}")
        return 1

    # Step 1: Get partition table
    print("\n--- Partition table ---")
    resp = tc.cmd("flash partitions", wait=5)
    print(resp.strip())

    dut_fw_offset = parse_partition_offset(resp, "dut_fw")
    prod_fw_offset = parse_partition_offset(resp, "prod_fw")

    if dut_fw_offset is None:
        print("\nERROR: dut_fw partition not found")
        tc.close()
        return 1

    print(f"\ndut_fw  offset: 0x{dut_fw_offset:08X}")
    if prod_fw_offset is not None:
        print(f"prod_fw offset: 0x{prod_fw_offset:08X}")

    # Step 2: Read dut_fw header (32 bytes)
    print(f"\n--- dut_fw header (32 bytes at 0x{dut_fw_offset:08X}) ---")
    resp = tc.cmd(f"flash read 0x{dut_fw_offset:08X} 32", wait=5)
    print(resp.strip())

    header_bytes = parse_hex_dump(resp)
    if len(header_bytes) < 32:
        print(f"\nERROR: only got {len(header_bytes)} bytes, need 32")
        tc.close()
        return 1

    # Parse dut_fw_hdr_t
    magic, size = struct.unpack_from("<II", header_bytes, 0)
    version_raw = header_bytes[8:24]
    version_str = version_raw.split(b"\x00")[0].decode(errors="replace")

    print(f"\n--- Parsed dut_fw_hdr_t ---")
    print(f"  magic   : 0x{magic:08X}  {'VALID' if magic == DUT_FW_MAGIC else 'INVALID'}")
    print(f"  size    : {size} bytes ({size / 1024:.1f} KB)")
    print(f"  version : '{version_str}'" if version_str else "  version : (empty)")
    print(f"  version hex: {version_raw.hex()}")

    # Step 3: Read prod_fw header if present
    if prod_fw_offset is not None:
        print(f"\n--- prod_fw header (32 bytes at 0x{prod_fw_offset:08X}) ---")
        resp = tc.cmd(f"flash read 0x{prod_fw_offset:08X} 32", wait=5)
        print(resp.strip())

        prod_bytes = parse_hex_dump(resp)
        if len(prod_bytes) >= 32:
            p_magic, p_size = struct.unpack_from("<II", prod_bytes, 0)
            p_ver_raw = prod_bytes[8:24]
            p_ver_str = p_ver_raw.split(b"\x00")[0].decode(errors="replace")
            print(f"\n--- Parsed prod_fw_hdr_t ---")
            print(f"  magic   : 0x{p_magic:08X}  {'VALID' if p_magic == DUT_FW_MAGIC else 'INVALID'}")
            print(f"  size    : {p_size} bytes ({p_size / 1024:.1f} KB)")
            print(f"  version : '{p_ver_str}'" if p_ver_str else "  version : (empty)")

    # Step 4: Check NVS keys for comparison
    print("\n--- NVS firmware keys ---")
    resp = tc.cmd("nvs get firmware dut_pfw_ver", wait=3)
    nvs_pfw = resp.strip()
    print(f"  dut_pfw_ver    : {nvs_pfw}")

    resp = tc.cmd("nvs get firmware product_fw_ver", wait=3)
    nvs_prod = resp.strip()
    print(f"  product_fw_ver : {nvs_prod}")

    tc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

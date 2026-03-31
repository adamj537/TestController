#!/usr/bin/env python3
"""Read DUT flash via SWD to identify what firmware is loaded.

Checks:
  1. Magic word at 0x08000200 — 0xC0DEBABE means PFW is loaded
  2. st_GlobHead_t at 0x08007FC0 — product FW version (issue.release.point)

If magic is 0xC0DEBABE → PFW is present (product FW NOT loaded).
If magic is absent (0x00000000 or anything else) → product FW is likely loaded;
reads GlobHead to confirm version.

Usage:
    python3 scripts/swd_read_dut_fw.py
    python3 scripts/swd_read_dut_fw.py --host 192.168.50.21
"""

import argparse
import re
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT

PFW_MAGIC_ADDR   = 0x08000200
PFW_MAGIC_VALUE  = 0xC0DEBABE
GLOB_HEAD_ADDR   = 0x08007FC0


class TC:
    def __init__(self, host: str, port: int = PORT) -> None:
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(10)
        self.sock.connect((self.host, self.port))
        self.sock.settimeout(0.5)
        # drain banner
        try:
            while self.sock.recv(4096):
                pass
        except socket.timeout:
            pass
        self.sock.settimeout(10)

    def cmd(self, command: str, wait: float = 3.0) -> str:
        assert self.sock
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


def parse_swd_word(resp: str, addr: int) -> int | None:
    """Parse '0x08000200: 0xC0DEBABE' → integer value, or None on failure."""
    pattern = rf"0x{addr:08X}:\s*0x([0-9A-Fa-f]{{8}})"
    m = re.search(pattern, resp, re.IGNORECASE)
    if m:
        return int(m.group(1), 16)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Read DUT flash via SWD to check firmware type")
    parser.add_argument("--host", default=HOST, help=f"TC IP (default: {HOST})")
    args = parser.parse_args()

    print(f"Connecting to TC console {args.host}:{PORT} ...")
    tc = TC(args.host)
    try:
        tc.connect()
    except Exception as e:
        print(f"  FAILED: {e}")
        return 1
    print("  Connected\n")

    try:
        # Pause autostart so DUT detect doesn't kick off a test
        tc.cmd("dut pause", wait=2)

        # Check DUT is seated
        resp = tc.cmd("dut sample", wait=5)
        adc_mv = -1
        for line in resp.splitlines():
            if "DUT sample:" in line and "mV" in line:
                try:
                    adc_mv = int(line.split(":")[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
        present = 0 < adc_mv < 2000
        print(f"DUT presence: ADC = {adc_mv} mV  → {'PRESENT' if present else 'ABSENT'}")
        if not present:
            print("ERROR: DUT not seated — cannot read SWD")
            return 1

        # Power DUT (VDUT1 only)
        print("Powering DUT (VDUT1 = 3300 mV) ...")
        tc.cmd("vdac_voltage 1 3300", wait=2)
        time.sleep(1.0)  # wait for power rail, no full boot needed for SWD

        # ── Read magic word ───────────────────────────────────────────────────
        print(f"\nReading magic @ 0x{PFW_MAGIC_ADDR:08X} ...")
        resp = tc.cmd(f"swd read {PFW_MAGIC_ADDR:08X} 1", wait=5)
        print(f"  Raw: {resp.strip()}")
        magic = parse_swd_word(resp, PFW_MAGIC_ADDR)

        if magic is None:
            print("  [FAIL] SWD read failed — check pogo contact / DUT power")
            return 1

        print(f"  Magic = 0x{magic:08X}")

        if magic == PFW_MAGIC_VALUE:
            print("\n  *** PFW IS LOADED (0xC0DEBABE present) — product FW NOT on DUT ***")
            # Still read GlobHead in case a stale header exists
            print(f"\nReading GlobHead @ 0x{GLOB_HEAD_ADDR:08X} (informational) ...")
            resp2 = tc.cmd(f"swd read {GLOB_HEAD_ADDR:08X} 2", wait=5)
            print(f"  Raw: {resp2.strip()}")
            return 0

        # ── Magic absent — read GlobHead for product FW version ───────────────
        print(f"\n  Magic ≠ 0xC0DEBABE — checking for product FW ...")
        print(f"Reading GlobHead @ 0x{GLOB_HEAD_ADDR:08X} ...")
        resp2 = tc.cmd(f"swd read {GLOB_HEAD_ADDR:08X} 2", wait=5)
        print(f"  Raw: {resp2.strip()}")

        word0 = parse_swd_word(resp2, GLOB_HEAD_ADDR)
        word1 = parse_swd_word(resp2, GLOB_HEAD_ADDR + 4)

        if word0 is None or word1 is None:
            print("  [FAIL] Could not read GlobHead")
            return 1

        fw_issue   = (word0 >> 24) & 0xFF
        fw_release =  word1        & 0xFF
        fw_point   = (word1 >>  8) & 0xFF

        if fw_issue == 0 and fw_release == 0 and fw_point == 0:
            print("  GlobHead all zeros — flash may be blank or erased")
            return 1

        print(f"\n  *** PRODUCT FW LOADED — version {fw_issue}.{fw_release}.{fw_point} ***")
        return 0

    finally:
        tc.cmd("vdac off", wait=2)
        tc.cmd("dut resume", wait=2)
        tc.close()
        print("\n[teardown] VDUT off, autostart resumed")


if __name__ == "__main__":
    sys.exit(main())

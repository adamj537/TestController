#!/usr/bin/env python3
"""diag_heartbeat_swd.py — Post-SWD-probe heartbeat test + vector table check.

Sequence:
  1. Connect, pause dut_detect
  2. Read DUT vector table via SWD (confirms valid PFW)
  3. SWD probe (power-cycles DUT) → immediately read INA219 current
  4. Wait for boot, then sample heartbeat + INA219
  5. If heartbeat fails: try manual power cycle and retry
  6. Teardown

Usage:
    python3 scripts/diag_heartbeat_swd.py [--host 192.168.50.21]
"""

import argparse
import socket
import sys
import time

from tc_config import HOST, PORT

INA219_CURRENT_LSB_UA = 10


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

    def cmd(self, command: str, wait: float = 3.0) -> str:
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

    def cmd_long(self, command: str, stop_marker: str = "Results:",
                 timeout: float = 10.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + timeout
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk:
                        time.sleep(0.3)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except socket.timeout:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


def parse_i2c_read(resp: str) -> int:
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def read_ina_quick(tc: TC, addr: int) -> tuple[int, float]:
    bus_raw = parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x02 2", wait=2))
    cur_raw = parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x04 2", wait=2))
    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    cur_ma = cur_ua / 1000.0 if cur_ua >= 0 else -1.0
    return bus_mv, cur_ma


def section(n: int, title: str) -> None:
    print(f"\n{'='*60}")
    print(f"[{n}] {title}")
    print(f"{'='*60}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Post-SWD heartbeat diagnostic")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    tc = TC(args.host)

    section(1, f"Connect to {args.host}:{PORT}")
    try:
        tc.connect()
        print("  OK")
    except Exception as e:
        print(f"  FAIL: {e}")
        return 1

    tc.cmd("dut pause", wait=2)
    print("  dut_detect paused")

    # ── 2. Init INA219 ──────────────────────────────────────────────────
    section(2, "Init INA219 #0")
    tc.cmd("i2c write 0x40 0x00 0x21 0x9F", wait=2)
    tc.cmd("i2c write 0x40 0x05 0xA0 0x00", wait=2)
    print("  INA219 configured")

    # ── 3. Vector table dump ──────────────────────────────────────────
    section(3, "DUT vector table (SWD read)")
    # Need VDUT on for SWD
    tc.cmd("vdac_voltage 1 3300", wait=5)
    tc.cmd("mux select 0 0", wait=2)
    time.sleep(1)
    # Read first 32 bytes of vector table
    resp = tc.cmd("swd read 0x08000000 8", wait=8)
    print(resp.strip())
    # Read PFW magic + info block
    resp = tc.cmd("swd read 0x08000200 8", wait=8)
    print(resp.strip())

    # ── 4. Check NRST and BOOT0 via SWD register reads ──────────────
    section(4, "STM32 option bytes (SWD read)")
    # Option byte register at 0x1FFF7800 on STM32L476
    resp = tc.cmd("swd read 0x1FFF7800 4", wait=8)
    print("  Option bytes:")
    print(resp.strip())
    # FLASH_OPTR at 0x40022020
    resp = tc.cmd("swd read 0x40022020 4", wait=8)
    print("  FLASH_OPTR:")
    print(resp.strip())

    # ── 5. Clean power cycle and immediate current profile ──────────
    section(5, "Clean power cycle: VDUT off 2s, on, PB-A, current profile")
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=3)
    print("  VDUT off, waiting 2s...")
    time.sleep(2)

    # Re-init INA219 after power off
    tc.cmd("i2c write 0x40 0x00 0x21 0x9F", wait=2)
    tc.cmd("i2c write 0x40 0x05 0xA0 0x00", wait=2)

    tc.cmd("vdac_voltage 1 3300", wait=5)
    time.sleep(0.5)
    print("  VDUT1 on at 3300 mV")

    v, i = read_ina_quick(tc, 0x40)
    print(f"  Pre-PBA: Vbus={v} mV, I={i:.1f} mA")

    tc.cmd("mux select 0 0", wait=2)
    print("  PB-A asserted")

    # Current profile: 10 samples over 5s
    print("  --- Current profile after PB-A ---")
    for n in range(10):
        time.sleep(0.5)
        v, i = read_ina_quick(tc, 0x40)
        t = (n + 1) * 0.5
        print(f"    t={t:.1f}s  Vbus={v} mV  I={i:.1f} mA")

    # ── 6. Heartbeat test ───────────────────────────────────────────
    section(6, "selftest heartbeat")
    resp = tc.cmd_long("selftest heartbeat", stop_marker="Results:", timeout=10)
    print(resp.strip())

    hb_pass = "[PASS]" in resp and "heartbeat" in resp.lower()

    if hb_pass:
        print("\n  >>> HEARTBEAT PASS — DUT booted successfully <<<")
    else:
        print("\n  >>> HEARTBEAT STILL FAILING <<<")
        print("  Trying: selftest dut_version (sends ENTER_TEST mode)")
        resp = tc.cmd_long("selftest dut_version", stop_marker="Results:", timeout=12)
        print(resp.strip())

        # Check DUT UART response
        print("\n  DUT UART probe:")
        resp = tc.cmd("dut version", wait=5)
        print(resp.strip())

        # Read PA9 state directly
        print("\n  PA9 via tie read 2 2:")
        resp = tc.cmd("tie read 2 2", wait=3)
        print(resp.strip())

        # Read GPIOA ODR (output data register) via SWD to see PA9 state from MCU side
        # GPIOA base = 0x48000000, ODR offset = 0x14
        print("\n  GPIOA ODR via SWD (bit 9 = PA9):")
        resp = tc.cmd("swd read 0x48000014 1", wait=5)
        print(resp.strip())

        # GPIOA MODER (mode register) — check if PA9 is configured as output
        # GPIOA base = 0x48000000, MODER offset = 0x00
        print("\n  GPIOA MODER via SWD (bits 19:18 = PA9 mode):")
        resp = tc.cmd("swd read 0x48000000 1", wait=5)
        print(resp.strip())

        # RCC clock status — is GPIOA clock enabled?
        # RCC_AHB2ENR = 0x4002104C
        print("\n  RCC_AHB2ENR via SWD (bit 0 = GPIOA EN):")
        resp = tc.cmd("swd read 0x4002104C 1", wait=5)
        print(resp.strip())

    # ── 7. Teardown ────────────────────────────────────────────────
    section(7, "Teardown")
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=3)
    tc.cmd("dut resume", wait=2)
    print("  Done")

    tc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

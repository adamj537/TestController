#!/usr/bin/env python3
"""diag_heartbeat_deep.py — Deep heartbeat regression diagnostic.

Sequence:
  1. Connect, pause dut_detect
  2. Clean slate: vdac off, mux release
  3. Read INA219 baseline (VDUT off, PB-A released)
  4. Enable VDUT1 at 3300 mV
  5. Read INA219 with VDUT on but PB-A released
  6. Assert PB-A
  7. Read INA219 at 0.5s intervals for 5s (current profile during boot)
  8. Read ADC128 CH2 (PA9/heartbeat) at 0.5s intervals
  9. Run selftest heartbeat
 10. Run selftest adc (full channel dump)
 11. SWD probe + read 0x08000200 (PFW magic)
 12. Teardown

Usage:
    python3 scripts/diag_heartbeat_deep.py [--host 192.168.50.21]
"""

import argparse
import re
import socket
import sys
import time

from tc_config import HOST, PORT


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


INA219_CURRENT_LSB_UA = 10  # 10 µA per LSB


def parse_i2c_read(resp: str) -> int:
    """Extract 16-bit value from 'N bytes]: XX YY' response."""
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def adc128_to_mv(raw: int) -> int:
    """Convert ADC128D818 raw 12-bit value to millivolts (2560 mV VREF)."""
    if raw < 0:
        return -1
    # ADC128D818: upper 12 bits of 16-bit read
    code = (raw >> 4) & 0x0FFF
    return int(code * 2560 / 4095)


def read_ina(tc: "TC", addr: int, label: str) -> tuple[int, float]:
    """Read INA219 bus voltage and current. Returns (mv, ma)."""
    bus_raw = parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x02 2", wait=2))
    cur_raw = parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x04 2", wait=2))
    sht_raw = parse_i2c_read(tc.cmd(f"i2c read 0x{addr:02x} 0x01 2", wait=2))

    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    cur_ma = cur_ua / 1000.0 if cur_ua >= 0 else -1.0
    sht_uv = sht_raw * 10 if sht_raw >= 0 else -1

    print(f"  {label} (0x{addr:02X}):")
    print(f"    Bus voltage : {bus_mv} mV")
    print(f"    Current     : {cur_ua} uA  ({cur_ma:.1f} mA)")
    print(f"    Shunt       : {sht_uv} uV")
    return bus_mv, cur_ma


def read_ina_quick(tc: "TC", addr: int) -> tuple[int, float]:
    """Quick INA219 read — bus voltage + current only."""
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
    parser = argparse.ArgumentParser(description="Deep heartbeat regression diagnostic")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    tc = TC(args.host)

    # ── 1. Connect ──────────────────────────────────────────────────────
    section(1, f"Connect to {args.host}:{PORT}")
    try:
        tc.connect()
        print("  OK")
    except Exception as e:
        print(f"  FAIL: {e}")
        return 1

    # ── 2. Clean slate ──────────────────────────────────────────────────
    section(2, "Clean slate: dut pause, mux release, vdac off")
    print(tc.cmd("dut pause", wait=2).strip())
    print(tc.cmd("mux release", wait=2).strip())
    print(tc.cmd("vdac off", wait=3).strip())
    time.sleep(1)

    # ── 3. Init INA219 #0 (VDUT1) — config + calibration ─────────────
    section(3, "Init INA219 #0 (config + cal)")
    print(tc.cmd("i2c write 0x40 0x00 0x21 0x9F", wait=2).strip())  # config
    print(tc.cmd("i2c write 0x40 0x05 0xA0 0x00", wait=2).strip())  # cal
    time.sleep(0.5)

    # ── 4. INA219 baseline (no power) ──────────────────────────────────
    section(4, "INA219 baseline (VDUT off)")
    read_ina(tc, 0x40, "VDUT1 baseline")

    # ── 5. Enable VDUT1 ────────────────────────────────────────────────
    section(5, "Enable VDUT1 at 3300 mV")
    resp = tc.cmd("vdac_voltage 1 3300", wait=5)
    print(resp.strip())
    time.sleep(1.0)

    # ── 6. INA219 with VDUT on, PB-A released ─────────────────────────
    section(6, "INA219 with VDUT1 on, PB-A released")
    read_ina(tc, 0x40, "VDUT1 powered, PB-A off")

    # ── 7. Assert PB-A ─────────────────────────────────────────────────
    section(7, "Assert PB-A (mux select 0 0)")
    resp = tc.cmd("mux select 0 0", wait=2)
    print(resp.strip())

    # ── 8. INA219 current profile during boot ──────────────────────────
    section(8, "INA219 current profile (5s, every 0.5s)")
    for i in range(10):
        time.sleep(0.5)
        v_mv, i_ma = read_ina_quick(tc, 0x40)
        t = (i + 1) * 0.5
        print(f"  t={t:.1f}s  Vbus={v_mv} mV  I={i_ma:.1f} mA")

    # ── 9. ADC128 CH2 reads (PA9/heartbeat signal) ────────────────────
    section(9, "ADC128 CH2 (PA9) — select MUX2 ch2, then read")
    # First select the right mux channel for PA9 routing
    print(tc.cmd("tie read 2 2", wait=3).strip())  # TIE MUX2 ch2 = PA9/SS_ENA_A
    time.sleep(0.5)
    for i in range(6):
        resp = tc.cmd("i2c read 0x1D 0x22 2", wait=2)
        parsed = parse_i2c_read(resp)
        mv = adc128_to_mv(parsed) if parsed >= 0 else -1
        print(f"  t={i*0.5:.1f}s  CH2 raw=0x{parsed:04X}  ≈{mv} mV" if parsed >= 0
              else f"  t={i*0.5:.1f}s  CH2 read error")
        time.sleep(0.5)

    # ── 10. selftest heartbeat ──────────────────────────────────────────
    section(10, "selftest heartbeat")
    resp = tc.cmd_long("selftest heartbeat", stop_marker="Results:", timeout=10)
    print(resp.strip())

    # ── 11. Read DUT current after heartbeat test ───────────────────────
    section(11, "INA219 after heartbeat test (power still on?)")
    read_ina(tc, 0x40, "VDUT1 post-heartbeat")

    # ── 12. SWD probe + PFW magic ──────────────────────────────────────
    section(12, "SWD probe + PFW magic word")
    resp = tc.cmd_long("swd probe", stop_marker="g3-tc|", timeout=10)
    print(resp.strip())
    time.sleep(0.5)
    resp = tc.cmd("swd read 0x08000200 4", wait=5)
    print(resp.strip())

    # ── 13. Teardown ───────────────────────────────────────────────────
    section(13, "Teardown")
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=3)
    tc.cmd("dut resume", wait=2)
    print("  PB-A released, VDUT off, dut_detect resumed")

    tc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify PB-A power assertion using raw GPIO — no firmware mux_select needed.

Sequence:
  1. dut stop + 2s wait    — halt detect task so it doesn't fight SIG
  2. vdac_duty 1 80        — VDUT1 on ~4.1V (80% duty cycle)
  3. INA219 baseline read  — expect ~4V bus, near-zero current (no digital rail)
  4. gpio set 17 0         — MUX A0 = 0  ┐
     gpio set 18 0         — MUX A1 = 0  ├ address ch0 (PB-A)
     gpio set 21 0         — MUX A2 = 0  ┘
     gpio set 0  0         — MUX SIG = LOW  (active-low PB-A assert)
  5. Wait 500ms            — 3V digital rail stabilises
  6. INA219 post-PBA read  — expect ~4V bus, >40 mA (DUT boots, digital rail on)
  7. Ctrl-C cleanup        — gpio set 0 1 (SIG HIGH), dut start

Pass criteria:
  - Baseline current < 5 mA
  - Post-PBA current > 40 mA
"""

import socket
import sys
import time
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(__file__))
from tc_config import HOST, PORT

# INA219: 10 µA/bit (matches firmware calibration)
INA219_CURRENT_LSB_UA: int = 10


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


def _print_resp(cmd: str, resp: str) -> None:
    print(f"  >>> {cmd}")
    for line in resp.splitlines():
        if line.strip() and "g3-tc|" not in line:
            print(f"      {line}")


def _parse_i2c_read(resp: str) -> int:
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def _signed16(raw: int) -> int:
    return raw - 0x10000 if raw > 0x7FFF else raw


def read_ina219(s: socket.socket, addr: int, label: str) -> tuple[int, int]:
    """Read INA219 bus voltage (mV) and current (µA). Returns (bus_mv, cur_ua)."""
    send_cmd(s, f"i2c write 0x{addr:02x} 0x00 0x21 0x9F", wait=0.1)  # config
    send_cmd(s, f"i2c write 0x{addr:02x} 0x05 0xA0 0x00", wait=0.1)  # cal
    time.sleep(0.5)
    bus_raw = _parse_i2c_read(send_cmd(s, f"i2c read 0x{addr:02x} 0x02 2"))
    cur_raw = _parse_i2c_read(send_cmd(s, f"i2c read 0x{addr:02x} 0x04 2"))
    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = _signed16(cur_raw) * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    return bus_mv, cur_ua


def main() -> int:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.settimeout(3.0)
        try:
            s.recv(4096)  # drain banner
        except socket.timeout:
            pass

        # Configure all MUX control pins as outputs once — never changes in this script
        send_cmd(s, "gpio mode 0 out")   # SIG
        send_cmd(s, "gpio mode 17 out")  # A0
        send_cmd(s, "gpio mode 18 out")  # A1
        send_cmd(s, "gpio mode 21 out")  # A2

        # ── Step 1: stop DUT detect ──────────────────────────────────────────
        print("\n[1] Stopping DUT detect task ...")
        _print_resp("dut stop", send_cmd(s, "dut stop"))
        print("    Waiting 2s for task exit ...")
        time.sleep(2.0)

        # ── Step 2: enable VDUT1 ─────────────────────────────────────────────
        print("\n[2] Enabling VDUT1 at 80% duty (~4.1V) ...")
        _print_resp("vdac_duty 1 80", send_cmd(s, "vdac_duty 1 80"))
        time.sleep(0.3)

        # ── Step 3: baseline INA219 read (no PBA) ────────────────────────────
        print("\n[3] Baseline INA219 read (PBA NOT asserted — expect ~0 mA) ...")
        bus_pre, cur_pre = read_ina219(s, 0x40, "VDUT1 (INA219 #0)")
        print(f"    VDUT1 (INA219 #0): {bus_pre} mV  {cur_pre / 1000:.1f} mA")

        # ── Step 4: set MUX address for ch0, drive SIG LOW ───────────────────
        print("\n[4] Asserting PB-A via raw GPIO ...")
        # Address lines: ch0 = A2:A1:A0 = 0:0:0
        _print_resp("gpio set 17 0 (A0=0)", send_cmd(s, "gpio set 17 0"))
        _print_resp("gpio set 18 0 (A1=0)", send_cmd(s, "gpio set 18 0"))
        _print_resp("gpio set 21 0 (A2=0)", send_cmd(s, "gpio set 21 0"))
        # Pre-drive SIG HIGH first: gpio_config(OUTPUT) uses the stale output
        # register value (=1 from DUT detect's last mux_select(3,1)).  Calling
        # gpio set 0 1 enters OUTPUT mode with register=1 — no glitch since the
        # pad was already HIGH (pulled up externally while in INPUT/float).
        # Then gpio set 0 0 is a clean HIGH→LOW falling edge with no prior spike.
        _print_resp("gpio set 0 1 (SIG=HIGH, enter OUTPUT mode cleanly)", send_cmd(s, "gpio set 0 1"))
        # SIG LOW — asserts PB-A (active-low), clean falling edge
        _print_resp("gpio set 0 0 (SIG=LOW → PBA)", send_cmd(s, "gpio set 0 0"))

        # ── Step 5: wait for 3V digital rail ─────────────────────────────────
        print("\n[5] Waiting 500ms for 3V digital rail ...")
        time.sleep(0.5)

        # ── Step 6: post-PBA INA219 read ─────────────────────────────────────
        print("\n[6] Post-PBA INA219 read (expect >40 mA) ...")
        bus_post, cur_post = read_ina219(s, 0x40, "VDUT1 (INA219 #0)")
        print(f"    VDUT1 (INA219 #0): {bus_post} mV  {cur_post / 1000:.1f} mA")

        # ── Result ─────────────────────────────────────────────────────────
        print()
        baseline_ok = cur_pre < 5_000     # < 5 mA baseline
        pba_ok      = cur_post > 40_000   # > 40 mA with PBA
        print(f"  Baseline current : {cur_pre  / 1000:.1f} mA  {'OK' if baseline_ok else 'UNEXPECTED'}")
        print(f"  Post-PBA current : {cur_post / 1000:.1f} mA  {'PASS' if pba_ok else 'FAIL — DUT not powered'}")
        if pba_ok:
            print("\n  PB-A ASSERTION VERIFIED — digital rail is up")
        else:
            print("\n  PB-A ASSERTION FAILED — digital rail did not latch")

        # ── Hold until Ctrl-C, then clean up ─────────────────────────────────
        print("\nHolding PB-A asserted.  Press Ctrl-C to release and exit.")
        try:
            while True:
                time.sleep(5)
                try:
                    s.sendall(b"\n")
                    s.recv(256)
                except Exception:
                    pass
        except KeyboardInterrupt:
            print("\nReleasing ...")
            _print_resp("gpio set 0 1 (SIG=HIGH → PBA release)", send_cmd(s, "gpio set 0 1"))
            _print_resp("dut start", send_cmd(s, "dut start"))

    return 0 if pba_ok else 1


if __name__ == "__main__":
    sys.exit(main())

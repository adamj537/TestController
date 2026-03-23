#!/usr/bin/env python3
"""Test SWD flash timing and KEEPALIVE for PFW and production firmware.

Measures:
    1. PFW flash time + KEEPALIVE hold after PB-A release
    2. Prod FW flash time + KEEPALIVE hold after PB-A release
    3. Current draw comparison at operating point

Usage:
    python3 scripts/test_swd_flash.py
"""

import socket
import sys
import time

HOST = "10.0.0.244"
PORT = 4242


class TC:
    def __init__(self):
        self.sock = None

    def connect(self):
        self.sock = socket.socket()
        self.sock.settimeout(120)
        self.sock.connect((HOST, PORT))
        time.sleep(0.5)
        try:
            self.sock.recv(4096)
        except:
            pass

    def cmd(self, command, wait=3.0):
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + wait
        self.sock.settimeout(1)
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

    def cmd_long(self, command, stop_marker="[PASS]", timeout=120.0):
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk or "[FAIL]" in chunk:
                        time.sleep(0.5)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def read_ina219(self):
        """Read INA219 #0 bus voltage and current."""
        resp = self.cmd("i2c read 40 02 2", wait=2)
        bus_mv = 0
        for line in resp.split("\n"):
            if "0x40" in line and "reg 0x02" in line:
                parts = line.split(":")[-1].strip().split()
                raw = int(parts[0] + parts[1], 16)
                bus_mv = (raw >> 3) * 4

        resp = self.cmd("i2c read 40 04 2", wait=2)
        cur_ua = 0
        for line in resp.split("\n"):
            if "0x40" in line and "reg 0x04" in line:
                parts = line.split(":")[-1].strip().split()
                raw = int(parts[0] + parts[1], 16)
                if raw > 0x7FFF:
                    raw -= 0x10000
                cur_ua = raw * 10

        return bus_mv, cur_ua

    def reconnect(self):
        self.close()
        time.sleep(0.5)
        self.connect()

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def test_flash(tc, target, label):
    """Flash firmware, measure time, check KEEPALIVE."""
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")

    # Flash
    tc.reconnect()
    cmd = f"swd flash local --target {target}"
    print(f"  Flashing: {cmd}")
    t0 = time.time()
    resp = tc.cmd_long(cmd, stop_marker="swd flash", timeout=120)
    elapsed = time.time() - t0

    passed = "[PASS]" in resp
    # Extract size
    size = 0
    for line in resp.split("\n"):
        if "Loaded" in line and "bytes" in line:
            for word in line.split():
                if word.isdigit():
                    size = int(word)
                    break

    print(f"  Result: {'PASS' if passed else 'FAIL'}  {size} bytes  {elapsed:.1f}s")

    if not passed:
        print(f"  Response: {resp[-200:]}")
        return None

    # Release PB-A
    time.sleep(1)
    tc.reconnect()
    tc.cmd("mux release", wait=2)
    print(f"  PB-A released")

    # Wait for KEEPALIVE to take over
    time.sleep(1)

    # Read INA219
    tc.reconnect()
    bus_mv, cur_ua = tc.read_ina219()
    keepalive = bus_mv > 2000 and cur_ua > 1000
    print(f"  Bus voltage: {bus_mv} mV")
    print(f"  Current: {cur_ua} uA = {cur_ua/1000:.1f} mA")
    print(f"  KEEPALIVE: {'HOLDING' if keepalive else 'FAILED — power dropped'}")

    return {
        "target": target,
        "label": label,
        "size": size,
        "time_s": elapsed,
        "bus_mv": bus_mv,
        "cur_ma": cur_ua / 1000.0,
        "keepalive": keepalive,
        "passed": passed,
    }


def main():
    tc = TC()
    tc.connect()

    # Test PFW
    pfw = test_flash(tc, "pfw", "PFW (test firmware)")

    # Test Production FW
    prod = test_flash(tc, "prod", "Production firmware")

    # Summary
    print(f"\n{'='*60}")
    print(f"  Summary")
    print(f"{'='*60}")
    print(f"  {'Target':<25s} {'Size':>8s} {'Time':>8s} {'Current':>10s} {'KEEPALIVE':>10s}")
    print(f"  {'-'*25} {'-'*8} {'-'*8} {'-'*10} {'-'*10}")

    for r in [pfw, prod]:
        if r:
            print(f"  {r['label']:<25s} {r['size']:>7d}B {r['time_s']:>7.1f}s "
                  f"{r['cur_ma']:>8.1f}mA {'HOLD' if r['keepalive'] else 'FAIL':>10s}")

    tc.close()

    ok = all(r and r["passed"] and r["keepalive"] for r in [pfw, prod])
    print(f"\n  Result: {'ALL PASSED' if ok else 'FAILED'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

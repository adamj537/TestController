#!/usr/bin/env python3
"""Direct VDUT diagnostic — isolate pin config / race condition.

Sends TC console commands one at a time, checks state after each step.
Targets the VDUT1 rail: PWM on GPIO1, enable on GPIO46, INA219 ch0.

Usage:
    python3 scripts/diag_vdut.py
"""

import socket
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from tc_config import HOST, PORT


def tc_cmd(cmd: str, wait: float = 3.0) -> str:
    """Send a command to the TC TCP console and return the response."""
    s = socket.socket()
    s.settimeout(5)
    s.connect((HOST, PORT))
    time.sleep(0.2)
    s.settimeout(0.5)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
    s.sendall((cmd + "\r\n").encode())
    out = b""
    deadline = time.time() + wait
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
            if chunk:
                out += chunk
        except OSError:
            pass
    s.close()
    return out.decode("utf-8", errors="replace")


def section(title: str) -> None:
    print(f"\n{'─' * 50}")
    print(f"  {title}")
    print(f"{'─' * 50}")


def main() -> None:
    print("=" * 50)
    print("  VDUT1 Direct Diagnostic")
    print("=" * 50)

    # Step 0: Verify TC reachable
    section("Step 0: TC connectivity")
    try:
        resp = tc_cmd("version", wait=2.0)
        print(f"  version: {resp.strip().splitlines()[-1] if resp.strip() else 'no response'}")
    except OSError as e:
        print(f"  FAIL: TC not reachable at {HOST}:{PORT} — {e}")
        sys.exit(1)

    # Step 1: Check calibration state
    section("Step 1: Calibration check")
    resp = tc_cmd("cal vdut-duty 0 3300", wait=2.0)
    print(f"  cal vdut-duty 0 3300:\n  {resp.strip()}")

    # Step 2: Ensure clean state — vdac off first
    section("Step 2: Clean state (vdac off)")
    resp = tc_cmd("vdac off", wait=2.0)
    print(f"  vdac off: {resp.strip()}")
    time.sleep(1.0)

    # Step 3: Read baseline ADC/INA219 (VDUT should be 0)
    section("Step 3: Baseline readings (VDUT off)")
    resp = tc_cmd("selftest adc", wait=5.0)
    for line in resp.strip().splitlines():
        if any(k in line.lower() for k in ["vdut", "ina", "ch0", "ch1", "bus"]):
            print(f"  {line.strip()}")

    # Step 4: Enable VDUT1 via vdac_voltage
    section("Step 4: Enable VDUT1 (vdac_voltage 1 3300)")
    resp = tc_cmd("vdac_voltage 1 3300", wait=5.0)
    print(f"  response: {resp.strip()}")

    # Step 5: Wait and read ADC/INA219 (VDUT should be ~3300 mV)
    section("Step 5: Post-enable readings")
    time.sleep(1.0)
    resp = tc_cmd("selftest adc", wait=5.0)
    for line in resp.strip().splitlines():
        if any(k in line.lower() for k in ["vdut", "ina", "ch0", "ch1", "bus"]):
            print(f"  {line.strip()}")

    # Step 6: Try INA219 current read
    section("Step 6: INA219 current check")
    resp = tc_cmd("selftest ina", wait=5.0)
    print(f"  selftest ina:\n  " + "\n  ".join(resp.strip().splitlines()[-6:]))

    # Step 7: DUT detect
    section("Step 7: DUT detect")
    resp = tc_cmd("dut sample", wait=2.0)
    print(f"  dut sample: {resp.strip()}")

    # Step 8: Try sm start and capture PreGate log
    section("Step 8: sm start (PreGate)")
    resp = tc_cmd("sm start", wait=10.0)
    print(f"  sm start response:")
    for line in resp.strip().splitlines():
        print(f"    {line.strip()}")

    # Step 9: Clean up
    section("Step 9: Cleanup")
    resp = tc_cmd("sm abort", wait=2.0)
    print(f"  sm abort: {resp.strip().splitlines()[-1] if resp.strip() else 'sent'}")
    resp = tc_cmd("vdac off", wait=2.0)
    print(f"  vdac off: {resp.strip()}")

    print(f"\n{'=' * 50}")
    print("  Done")
    print("=" * 50)


if __name__ == "__main__":
    main()

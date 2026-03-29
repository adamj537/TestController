#!/usr/bin/env python3
"""
Hardware validation script — TC_SM_PRE_GATE feature.

Checks:
  1. WiFi connected
  2. DUT auto-start: insert DUT → selftest-only cycle fires
  3. Full gate: 'sm start' → PRECHECK → PRE_GATE → TESTING/FAIL
  4. SM settles to expected end state

Usage:
    python3 scripts/tc_validate_pre_gate.py

Environment:
    TC_HOST       — device IP  (default from tc_config / local_config)
    TC_PORT       — TCP port   (default from tc_config / local_config)
    TC_WIFI_SSID  — WiFi SSID  (default from tc_config / local_config)
    TC_WIFI_PW    — WiFi password (default from tc_config / local_config)

Requires:
    - TC device reachable at TC_HOST:TC_PORT
    - DUT physically inserted when prompted
"""

import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

from tc_console import TcConsole
from tc_config import WIFI_SSID as _CFG_SSID, WIFI_PASS as _CFG_PASS

WIFI_SSID: str = os.environ.get("TC_WIFI_SSID", _CFG_SSID)
WIFI_PW:   str = os.environ.get("TC_WIFI_PW",   _CFG_PASS)

PASS_STR = "\033[32mPASS\033[0m"
FAIL_STR = "\033[31mFAIL\033[0m"
STEP_STR = "\033[34m---\033[0m"

results: list[tuple[str, bool, str]] = []


def check(label: str, condition: bool, detail: str = "") -> bool:
    status = PASS_STR if condition else FAIL_STR
    print(f"  [{status}] {label}" + (f" — {detail}" if detail else ""))
    results.append((label, condition, detail))
    return condition


def wifi_connect() -> bool:
    """Issue wifi connect and verify IP is assigned on a fresh connection."""
    print("Connecting WiFi...")
    try:
        with TcConsole() as tc:
            tc.cmd(f"wifi connect {WIFI_SSID} {WIFI_PW}", wait=1.0)
    except Exception:
        pass  # connection may drop during association — expected

    print("  (waiting for WiFi association...)")
    time.sleep(7)

    try:
        with TcConsole() as tc2:
            r = tc2.cmd("wifi status", wait=2.0)
    except OSError as e:
        print(f"  Reconnect failed: {e}")
        return False

    print(r)
    return "ip:" in r.lower() or "connected" in r.lower() or "10.0.0" in r


def main() -> int:
    # 1. WiFi
    print(f"{STEP_STR} Step 1: WiFi")
    ok = wifi_connect()
    check("WiFi connected", ok)
    if not ok:
        print("WiFi failed — aborting")
        return 1

    # Fresh connection after WiFi provisioning may have disrupted the TCP session
    tc = TcConsole()
    tc.connect()

    # 2. SM status
    print(f"\n{STEP_STR} Step 2: SM status at boot")
    r = tc.cmd("sm status", wait=1.0)
    print(r)
    check("SM in IDLE at boot", "idle" in r.lower(), r.strip())

    # 3. DUT presence / auto-start
    print(f"\n{STEP_STR} Step 3: DUT auto-start (selftest-only)")
    print("  >> Ensure DUT is NOT inserted, then insert it now.")
    input("  Press Enter when DUT is inserted...")
    time.sleep(2)
    r = tc.cmd("sm status", wait=3.0)
    print(r)
    # After selftest-only cycle, should return to IDLE quickly
    time.sleep(3)
    r2 = tc.cmd("sm status", wait=1.0)
    print(r2)
    check("SM returned to IDLE after selftest-only", "idle" in r2.lower(), r2.strip())

    # 4. Full gate via sm start
    print(f"\n{STEP_STR} Step 4: Full pre-gate sequence ('sm start')")
    r = tc.cmd("sm start", wait=0.5)
    print(r)
    time.sleep(0.5)

    # Poll status for up to 10s to observe PRE_GATE
    saw_pre_gate = False
    for _ in range(20):
        r = tc.cmd("sm status", wait=0.3)
        state = r.lower()
        print(f"  sm status: {r.strip()}")
        if "pre_gate" in state or "pregate" in state or "pre-gate" in state:
            saw_pre_gate = True
            break
        if "idle" in state or "fail" in state:
            break
        time.sleep(0.5)

    check("TC_SM_PRE_GATE state observed", saw_pre_gate)

    # Wait for completion
    time.sleep(4)
    r = tc.cmd("sm status", wait=1.0)
    print(f"  Final SM status: {r.strip()}")
    check("SM settled (IDLE or TESTING or FAIL)", any(
        x in r.lower() for x in ("idle", "testing", "fail")
    ), r.strip())

    tc.close()

    # Summary
    total = len(results)
    passed = sum(1 for _, ok, _ in results if ok)
    print(f"\n{'='*40}")
    print(f"Results: {passed}/{total} passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())

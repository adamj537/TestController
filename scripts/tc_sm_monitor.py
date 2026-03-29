#!/usr/bin/env python3
"""
Run 'sm start' and poll SM status every 500ms for up to 30s.
Captures state transitions and prints a timeline.

Usage:
    python3 scripts/tc_sm_monitor.py [--no-start]   # --no-start: just poll, don't trigger
"""

import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

from tc_console import TcConsole

NO_START = "--no-start" in sys.argv

tc = TcConsole()
tc.connect()

if not NO_START:
    print(">>> sm start")
    r = tc.cmd("sm start", wait=0.2)
    print(r)

print("\n--- Polling SM status (30s) ---")
states: list[tuple[float, str]] = []
t0 = time.monotonic()
last = ""
while (time.monotonic() - t0) < 30.0:
    r = tc.cmd("sm status", wait=0.1)
    # Extract state line
    line = ""
    for ln in r.splitlines():
        if "state:" in ln.lower() or "State" in ln:
            line = ln.strip()
            break
    if not line:
        line = r.strip().split("\n")[0] if r.strip() else "?"
    elapsed = time.monotonic() - t0
    if line != last:
        print(f"  t={elapsed:5.1f}s  {line}")
        states.append((elapsed, line))
        last = line
        # Stop when settled back to Idle or Fail
        if any(x in line.lower() for x in ("idle", "fail")) and elapsed > 2.0:
            break
    time.sleep(0.4)

tc.close()

print("\n--- Transition summary ---")
for i, (ts, st) in enumerate(states):
    if i > 0:
        dt = ts - states[i - 1][0]
        print(f"  {st}  (+{dt:.1f}s)")
    else:
        print(f"  {st}  (t=0)")

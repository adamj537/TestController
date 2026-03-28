#!/usr/bin/env python3
"""Test recipe engine — Phase 3 validation.

Tests:
    1. recipe run (hardcoded fallback) — executes via engine, matches selftest all
    2. recipe store + recipe run (from NVS) — round-trip JSON execution
    3. MQTT log has progress entries from engine run
"""

import json
import socket
import sys
import time

from tc_config import HOST, PORT


class TC:
    def __init__(self):
        self.sock = None

    def connect(self):
        self.sock = socket.socket()
        self.sock.settimeout(45)
        self.sock.connect((HOST, PORT))
        time.sleep(0.5)
        try:
            self.sock.recv(4096)
        except:
            pass

    def cmd(self, command, wait=3.0):
        self.sock.settimeout(max(wait + 5, 10))
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

    def cmd_long(self, command, stop_marker="===", timeout=40.0):
        self.sock.settimeout(timeout + 5)
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk:
                        time.sleep(0.5)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def main():
    passed = 0
    failed = 0

    # Test 1: recipe run (hardcoded fallback)
    print("[1] recipe run (hardcoded fallback)")
    tc = TC()
    tc.connect()
    resp = tc.cmd_long("recipe run", stop_marker="=== Recipe:", timeout=35)
    tc.close()

    pass_lines = [l for l in resp.split("\n") if "[PASS]" in l]
    fail_lines = [l for l in resp.split("\n") if "[FAIL]" in l]
    has_outcome = "outcome=" in resp
    outcome_pass = "outcome=PASS" in resp
    # vdut safety guard: intentionally fails when DUT is present in the nest
    vdut_only_fail = (len(fail_lines) == 1 and "VDUT" in fail_lines[0]
                      and "DUT detected" in fail_lines[0])

    if has_outcome and (outcome_pass or vdut_only_fail) and len(pass_lines) >= 4:
        note = " (vdut safety guard — DUT present)" if vdut_only_fail else ""
        print(f"  PASS: {len(pass_lines)} checks passed{note}")
        passed += 1
    else:
        print(f"  FAIL: {len(pass_lines)} pass, {len(fail_lines)} fail, outcome_pass={outcome_pass}")
        failed += 1

    time.sleep(2)

    # Test 2: recipe store default + recipe run default (from NVS)
    print("[2] recipe store default + recipe run default (NVS round-trip)")
    tc = TC()
    tc.connect()
    store_resp = tc.cmd("recipe store default", wait=3)
    stored = "stored" in store_resp.lower()
    tc.close()

    if not stored:
        print(f"  FAIL: store failed — {store_resp.strip()}")
        failed += 1
    else:
        time.sleep(1)
        tc = TC()
        tc.connect()
        resp = tc.cmd_long("recipe run default", stop_marker="=== Recipe:", timeout=35)
        tc.close()

        pass_lines = [l for l in resp.split("\n") if "[PASS]" in l]
        fail_lines = [l for l in resp.split("\n") if "[FAIL]" in l]
        has_outcome = "outcome=" in resp
        outcome_pass = "outcome=PASS" in resp
        vdut_only_fail = (len(fail_lines) == 1 and "VDUT" in fail_lines[0]
                          and "DUT detected" in fail_lines[0])

        if has_outcome and (outcome_pass or vdut_only_fail) and len(pass_lines) >= 4:
            note = " (vdut safety guard — DUT present)" if vdut_only_fail else ""
            print(f"  PASS: NVS recipe executed — {len(pass_lines)} checks{note}")
            passed += 1
        else:
            print(f"  FAIL: {len(pass_lines)} pass, outcome_pass={outcome_pass}")
            failed += 1

    time.sleep(2)

    # Test 3: MQTT log has progress from engine run
    print("[3] MQTT log has progress from engine run")
    tc = TC()
    tc.connect()
    resp = tc.cmd("mqtt log", wait=2)
    tc.close()

    entries = [l for l in resp.split("\n") if " TX " in l]
    has_progress = any("progress" in l for l in entries)
    progress_count = sum(1 for l in entries if "progress" in l)

    if has_progress and progress_count >= 6:
        print(f"  PASS: {progress_count} progress entries in MQTT log")
        passed += 1
    else:
        print(f"  FAIL: only {progress_count} progress entries")
        failed += 1

    # Summary
    total = passed + failed
    print(f"\nRecipe engine tests: {passed}/{total} passed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()

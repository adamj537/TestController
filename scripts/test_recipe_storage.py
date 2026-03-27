#!/usr/bin/env python3
"""Test recipe SPIFFS storage — Phase 4 validation.

Tests:
    1. recipe store test1 — stores to SPIFFS
    2. recipe store test2 — second recipe
    3. recipe list — shows both recipes
    4. recipe load test1 — round-trips correctly
    5. recipe run test1 — executes from SPIFFS
    6. recipe delete test2 — removes recipe
    7. recipe list — shows only test1 remaining
"""

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


def check(name, condition, detail=""):
    status = "\033[32mPASS\033[0m" if condition else "\033[31mFAIL\033[0m"
    line = f"  [{status}] {name}"
    if detail:
        line += f"  ({detail})"
    print(line)
    return 1 if condition else 0


def main():
    passed = 0
    total = 0

    # 1. Store test1
    print("[1] recipe store test1")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe store test1", wait=5)
    tc.close()
    total += 1
    passed += check("Store test1", "stored" in resp.lower())

    time.sleep(0.5)

    # 2. Store test2
    print("[2] recipe store test2")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe store test2", wait=5)
    tc.close()
    total += 1
    passed += check("Store test2", "stored" in resp.lower())

    time.sleep(0.5)

    # 3. List — expect both
    print("[3] recipe list (expect 2)")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe list", wait=3)
    tc.close()
    has_test1 = "test1" in resp
    has_test2 = "test2" in resp
    total += 1
    passed += check("List shows both", has_test1 and has_test2,
                     f"test1={'yes' if has_test1 else 'no'} test2={'yes' if has_test2 else 'no'}")

    time.sleep(0.5)

    # 4. Load test1
    print("[4] recipe load test1")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe load test1", wait=3)
    tc.close()
    has_steps = "prim=" in resp
    has_recipe = "Recipe:" in resp
    total += 1
    passed += check("Load test1", has_steps and has_recipe)

    time.sleep(0.5)

    # 5. Run test1
    print("[5] recipe run test1")
    tc = TC(); tc.connect()
    resp = tc.cmd_long("recipe run test1", stop_marker="=== Recipe:", timeout=35)
    tc.close()
    outcome_pass = "outcome=PASS" in resp
    total += 1
    passed += check("Run test1", outcome_pass)

    time.sleep(1)

    # 6. Delete test2
    print("[6] recipe delete test2")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe delete test2", wait=3)
    tc.close()
    total += 1
    passed += check("Delete test2", "deleted" in resp.lower())

    time.sleep(0.5)

    # 7. List — expect only test1
    print("[7] recipe list (expect 1)")
    tc = TC(); tc.connect()
    resp = tc.cmd("recipe list", wait=3)
    tc.close()
    has_test1 = "test1" in resp
    has_test2 = "test2" in resp
    total += 1
    passed += check("List shows only test1", has_test1 and not has_test2)

    print(f"\nRecipe storage tests: {passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()

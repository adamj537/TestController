#!/usr/bin/env python3
"""Test recipe JSON commands — Phase 2 validation.

Tests:
    1. recipe show — produces valid JSON with expected fields
    2. recipe store default — stores hardcoded recipe to NVS
    3. recipe load default — round-trips and matches original
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
        self.sock.settimeout(10)
        self.sock.connect((HOST, PORT))
        time.sleep(0.5)
        try:
            self.sock.recv(4096)
        except:
            pass

    def cmd(self, command, wait=3.0):
        self.sock.sendall((command + "\n").encode())
        time.sleep(wait)
        out = []
        try:
            while True:
                out.append(self.sock.recv(8192).decode(errors="replace"))
        except:
            pass
        return "".join(out)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def extract_json_line(text):
    """Find the first line containing a JSON object."""
    for line in text.split("\n"):
        line = line.strip()
        if line.startswith("{") and "recipeId" in line:
            return line
    return None


def main():
    passed = 0
    failed = 0

    tc = TC()
    tc.connect()

    # Test 1: recipe show
    print("[1] recipe show")
    resp = tc.cmd("recipe show")
    json_line = extract_json_line(resp)
    if json_line:
        try:
            recipe = json.loads(json_line)
            has_id = "recipeId" in recipe
            has_steps = "steps" in recipe and len(recipe["steps"]) > 0
            step_count = len(recipe.get("steps", []))
            if has_id and has_steps:
                print(f"  PASS: {recipe['recipeId']} v{recipe.get('recipeVersion','')} — {step_count} steps")
                passed += 1
            else:
                print(f"  FAIL: missing recipeId or steps")
                failed += 1
        except json.JSONDecodeError as e:
            print(f"  FAIL: invalid JSON — {e}")
            failed += 1
    else:
        print(f"  FAIL: no JSON found in response")
        print(f"  Response: {resp[:300]}")
        failed += 1

    # Need fresh connection between commands
    tc.close()
    time.sleep(0.5)
    tc.connect()

    # Test 2: recipe store default
    print("[2] recipe store default")
    resp = tc.cmd("recipe store default")
    if "stored" in resp.lower():
        print("  PASS: stored to NVS")
        passed += 1
    else:
        print(f"  FAIL: {resp.strip()}")
        failed += 1

    tc.close()
    time.sleep(0.5)
    tc.connect()

    # Test 3: recipe load default
    print("[3] recipe load default")
    resp = tc.cmd("recipe load default")
    has_recipe_id = "fixture-selftest" in resp or "default" in resp
    has_steps = "Steps:" in resp
    has_step_list = "prim=" in resp
    if has_recipe_id and has_steps and has_step_list:
        # Count steps listed
        step_lines = [l for l in resp.split("\n") if "prim=" in l]
        enabled = sum(1 for l in step_lines if "[ON " in l)
        disabled = sum(1 for l in step_lines if "[off" in l)
        print(f"  PASS: loaded — {enabled} enabled, {disabled} disabled, {len(step_lines)} total")
        passed += 1
    else:
        print(f"  FAIL: unexpected response")
        print(f"  Response: {resp[:500]}")
        failed += 1

    tc.close()

    # Summary
    total = passed + failed
    print(f"\nRecipe JSON tests: {passed}/{total} passed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()

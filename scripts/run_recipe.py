#!/usr/bin/env python3
"""run_recipe.py — Pause DUT auto-start, run a recipe, capture output.

Usage:
    python3 scripts/run_recipe.py [--host 10.0.0.244] [--recipe g3-mb-v2] [--timeout 120]
"""

import argparse
import socket
import sys
import time

HOST = "10.0.0.244"
PORT = 4242


def main():
    parser = argparse.ArgumentParser(description="Run a recipe on TC")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--recipe", default="g3-mb-v2")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()

    print(f"[1] Connecting to {args.host}:{PORT}")
    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((args.host, PORT))
    except Exception as e:
        print(f"  FAIL: {e}")
        sys.exit(1)

    time.sleep(0.5)
    # drain banner
    sock.settimeout(0.5)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass
    print("  Connected")

    def cmd(command, wait=3.0):
        sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + wait
        sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if "g3-tc|" in chunk and ">" in chunk:
                        break
            except socket.timeout:
                continue
        return "".join(out)

    print("\n[2] Pausing DUT auto-start")
    resp = cmd("dut pause", wait=2)
    print(f"  {resp.strip()[:120]}")

    print(f"\n[3] Running recipe: {args.recipe}")
    sock.sendall(f"recipe run {args.recipe}\n".encode())

    # Collect output until recipe completes or timeout
    out = []
    deadline = time.time() + args.timeout
    sock.settimeout(2.0)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                print(chunk, end="", flush=True)
                out.append(chunk)
                text = "".join(out)
                # Recipe engine prints outcome at end
                if "Recipe " in text and ("PASS" in text.split("Recipe ")[-1] or
                                           "FAIL" in text.split("Recipe ")[-1] or
                                           "ABORT" in text.split("Recipe ")[-1]):
                    # Wait a bit more for trailing output
                    time.sleep(1)
                    sock.settimeout(0.5)
                    try:
                        extra = sock.recv(8192).decode(errors="replace")
                        if extra:
                            print(extra, end="", flush=True)
                            out.append(extra)
                    except socket.timeout:
                        pass
                    break
        except socket.timeout:
            continue

    print("\n\n[4] Resuming DUT auto-start")
    resp = cmd("dut resume", wait=2)
    print(f"  {resp.strip()[:120]}")

    sock.close()

    text = "".join(out)
    if "PASS" in text and "Recipe" in text:
        print("\n[RESULT] Recipe PASSED")
        sys.exit(0)
    else:
        print("\n[RESULT] Recipe did not pass")
        sys.exit(1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Send a command to the TC via TCP console and print the response.

Usage:
    python3 scripts/tc_cmd.py "recipe show"
    python3 scripts/tc_cmd.py "recipe show" 5        # 5 second wait
    python3 scripts/tc_cmd.py "sm start" 40           # long timeout
    python3 scripts/tc_cmd.py "mqtt log"
"""

import socket
import sys
import time

HOST = "10.0.0.244"
PORT = 4242

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <command> [wait_seconds]")
        sys.exit(1)

    cmd = sys.argv[1]
    wait = float(sys.argv[2]) if len(sys.argv) >= 3 else 3.0

    s = socket.socket()
    s.settimeout(max(wait + 5, 10))
    s.connect((HOST, PORT))
    time.sleep(0.5)
    try:
        s.recv(4096)  # drain banner
    except:
        pass

    s.sendall((cmd + "\n").encode())

    # Read until we see the prompt or timeout
    out = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                # Check if prompt appeared (command complete)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except socket.timeout:
            continue
    s.close()

    print("".join(out))


if __name__ == "__main__":
    main()

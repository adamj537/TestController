#!/usr/bin/env python3
"""check_recipe_failures.py — Run sm start and report which recipe steps fail."""
import socket
import time
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from tc_config import HOST, PORT


def tc_cmd(cmd: str, wait: float = 3.0) -> str:
    s = socket.socket()
    s.connect((HOST, PORT))
    s.settimeout(1)
    time.sleep(0.2)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
    s.send((cmd + "\r\n").encode())
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


def main() -> None:
    print("=== sm start ===")
    result = tc_cmd("sm start", wait=60.0)
    print(result)

    print("\n=== mqtt log ===")
    print(tc_cmd("mqtt log", wait=3.0))


if __name__ == "__main__":
    main()

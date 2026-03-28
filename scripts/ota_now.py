#!/usr/bin/env python3
"""Send OTA update command to TC and monitor progress."""
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT

OTA_URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.50.41:8080/firmware.bin"

def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.socket()
    s.settimeout(10)
    s.connect((HOST, PORT))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass

    cmd = f"ota update {OTA_URL}"
    print(f"Sending: {cmd}")
    s.sendall((cmd + "\r\n").encode())

    # Monitor OTA progress for up to 120s
    deadline = time.time() + 120
    s.settimeout(2.0)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                for line in chunk.splitlines():
                    line = line.strip()
                    if line:
                        print(f"  {line}")
                    if "Rebooting" in line or "restart" in line.lower():
                        print("\nOTA complete — TC is rebooting.")
                        s.close()
                        return
        except socket.timeout:
            continue
        except OSError:
            print("\nConnection lost — TC likely rebooting.")
            return

    print("\nTimeout waiting for OTA completion.")
    s.close()

if __name__ == "__main__":
    main()

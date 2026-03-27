#!/usr/bin/env python3
"""Listen to TC console output for N seconds (default 15).

Usage:
    python3 scripts/monitor_raw.py [seconds] [ip]
"""
import socket
import sys
import time

ip = sys.argv[2] if len(sys.argv) > 2 else "10.0.0.244"
duration = int(sys.argv[1]) if len(sys.argv) > 1 else 15

sock = socket.socket()
sock.settimeout(10.0)
sock.connect((ip, 4242))
time.sleep(0.3)

# drain banner
sock.settimeout(0.2)
try:
    while sock.recv(4096):
        pass
except socket.timeout:
    pass

sock.settimeout(1.0)
end = time.time() + duration
buf = b""
while time.time() < end:
    try:
        chunk = sock.recv(4096)
        if chunk:
            buf += chunk
    except socket.timeout:
        pass

sock.close()
print(buf.decode("utf-8", errors="replace"))

#!/usr/bin/env python3
"""tc_vdac_help.py — Print vdac command help from TC console."""
import socket, time

TC_IP = '192.168.50.30'
TC_PORT = 4242


def connect(ip: str) -> socket.socket:
    s = socket.socket()
    s.settimeout(10)
    s.connect((ip, TC_PORT))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while True:
            s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(10)
    return s


def cmd(s: socket.socket, c: str, wait: float = 3.0) -> str:
    s.sendall((c + '\n').encode())
    out = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='replace')
            if chunk:
                out.append(chunk)
                if 'g3-tc|' in chunk and '>' in chunk:
                    break
        except socket.timeout:
            continue
    return ''.join(out)


s = connect(TC_IP)
print(cmd(s, 'help vdac', 3.0))
s.close()

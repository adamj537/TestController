#!/usr/bin/env python3
"""
tc_console.py — TCP console helper for TCC at 10.0.0.244:4242.

Issue: All sessions — standard pattern for sending commands to the TCC
via TCP console. Import this module in other scripts or run standalone.

Usage as module:
    from tc_console import TcConsole
    with TcConsole() as tc:
        print(tc.cmd('selftest all', wait=15))

Usage standalone:
    python3 scripts/tc_console.py 'selftest all'
    python3 scripts/tc_console.py 'wifi status'
"""
import socket
import sys
import time

import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(__file__))
from tc_config import HOST as TCC_IP, PORT as TCC_PORT


class TcConsole:
    def __init__(self, ip: str = TCC_IP, port: int = TCC_PORT, timeout: float = 10.0):
        self.ip      = ip
        self.port    = port
        self.timeout = timeout
        self._sock   = None

    def connect(self) -> None:
        self._sock = socket.socket()
        self._sock.settimeout(self.timeout)
        self._sock.connect((self.ip, self.port))
        time.sleep(0.3)
        self._drain()

    def _drain(self, wait: float = 0.3) -> str:
        time.sleep(wait)
        buf = b''
        self._sock.settimeout(0.1)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
                buf += chunk
        except socket.timeout:
            pass
        self._sock.settimeout(self.timeout)
        return buf.decode('utf-8', errors='replace')

    def cmd(self, command: str, wait: float = 0.4) -> str:
        """Send command, collect output until next prompt or wait expires."""
        self._sock.sendall((command + '\n').encode())
        time.sleep(wait)
        out = b''
        self._sock.settimeout(0.5)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        self._sock.settimeout(self.timeout)
        return out.decode('utf-8', errors='replace')

    def close(self) -> None:
        if self._sock:
            self._sock.close()
            self._sock = None

    def __enter__(self) -> 'TcConsole':
        self.connect()
        return self

    def __exit__(self, *_) -> None:
        self.close()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'Usage: {sys.argv[0]} <command> [wait_seconds]')
        sys.exit(1)
    command  = sys.argv[1]
    wait     = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
    with TcConsole() as tc:
        print(tc.cmd(command, wait=wait))

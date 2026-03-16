#!/usr/bin/env python3
"""
pb_hold_monitor.py — Hold DUT PB-A low for N seconds and confirm power latch.

Issue: S4 — DUT won't start without PB held low ~5s; power latch bringup.

Transport: TCP console 10.0.0.244:4242

PB-A path: TCC GPIO36 (SIG) → U8 MUX (A0=GPIO17, A1=GPIO18, A2=GPIO21)
  /EN=GPIO35 — assert low to enable U8
  Ch0 = PB-A

DUT latches when INA219 current rises above ~20 mA after PB release.

Usage:
  python3 scripts/pb_hold_monitor.py [--duty 80] [--hold 5]
"""
import socket
import sys
import time

TCC_IP      = '10.0.0.244'
DEFAULT_DUTY = 80  # %
HOLD_SEC     = 5   # seconds to hold PB-A low
LATCH_MA     = 20  # mA threshold for "DUT latched"


def connect() -> socket.socket:
    s = socket.socket()
    s.settimeout(5)
    s.connect((TCC_IP, 4242))
    time.sleep(0.3)
    _drain(s)
    return s


def _drain(s: socket.socket, wait: float = 0.3) -> str:
    time.sleep(wait)
    buf = b''
    s.settimeout(0.1)
    try:
        while True:
            buf += s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(5)
    return buf.decode('utf-8', errors='replace')


def cmd(s: socket.socket, c: str, wait: float = 0.2) -> str:
    s.sendall((c + '\n').encode())
    return _drain(s, wait)


def _parse_bytes(out: str) -> list[int]:
    for line in out.splitlines():
        if ':' not in line:
            continue
        after = line.split(':', 1)[1].strip()
        vals = []
        for p in after.split():
            try:
                vals.append(int(p, 16))
            except ValueError:
                pass
        if len(vals) >= 2:
            return vals
    return []


def read_ina219(s: socket.socket) -> tuple[int | None, float | None]:
    vb = _parse_bytes(cmd(s, 'i2c read 0x40 0x02 2', 0.4))
    ic = _parse_bytes(cmd(s, 'i2c read 0x40 0x04 2', 0.4))
    if len(vb) < 2 or len(ic) < 2:
        return None, None
    vbus_mv = ((((vb[0] << 8) | vb[1]) >> 3) & 0x1FFF) * 4
    raw_cur = (ic[0] << 8) | ic[1]
    if raw_cur > 0x7FFF:
        raw_cur -= 0x10000
    return vbus_mv, raw_cur * 10 / 1000.0


def main(duty: int = DEFAULT_DUTY, hold_sec: float = HOLD_SEC) -> None:
    s = connect()
    cmd(s, 'i2c init 15 16', 0.4)

    # Apply VDUT power
    cmd(s, f'vdac set 1 {duty}', 0.3)
    time.sleep(1)

    v, i = read_ina219(s)
    print(f'Baseline: {v} mV  {i:.1f} mA')

    # Configure U8 MUX address pins, assert PB-A (GPIO36 low)
    for pin, val in [(17, 0), (18, 0), (21, 0)]:
        cmd(s, f'gpio mode {pin} out', 0.1)
        cmd(s, f'gpio set  {pin} {val}', 0.1)
    cmd(s, 'gpio mode 35 out', 0.1)
    cmd(s, 'gpio set  35 0',   0.1)  # /EN low → enable U8
    cmd(s, 'gpio mode 36 out', 0.1)
    cmd(s, 'gpio set  36 0',   0.1)  # SIG low → PB-A asserted

    print(f'PB-A held low — monitoring for {hold_sec}s ...')
    for t in range(1, int(hold_sec) + 1):
        time.sleep(1)
        v, i = read_ina219(s)
        print(f'  t+{t}s: {v} mV  {i:.1f} mA')

    # Release PB-A
    cmd(s, 'gpio set 36 1', 0.1)  # SIG high → deassert
    cmd(s, 'gpio set 35 1', 0.1)  # /EN high → disable U8
    print('PB-A released.')
    time.sleep(1.5)

    v, i = read_ina219(s)
    print(f'After release: {v} mV  {i:.1f} mA')
    if i is not None and i > LATCH_MA:
        print(f'>>> LATCHED  ({i:.0f} mA > {LATCH_MA} mA threshold)')
    else:
        print(f'>>> Did NOT latch  ({i:.0f} mA)')

    s.close()


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='DUT PB-A hold + latch test')
    p.add_argument('--duty', type=int,   default=DEFAULT_DUTY)
    p.add_argument('--hold', type=float, default=HOLD_SEC,
                   help='Seconds to hold PB-A low (default 5)')
    args = p.parse_args()
    main(duty=args.duty, hold_sec=args.hold)

#!/usr/bin/env python3
"""
ina219_monitor.py — Monitor VDUT1/VDUT2 voltage and current via INA219.

Issue: S3 — INA219 VBUS chosen over ADC+divider for power delivery measurement;
VDUT characterization at multiple PWM duty points.

Transport: TCP console 10.0.0.244:4242

Usage:
  python3 scripts/ina219_monitor.py [--duty 80] [--duration 10] [--limit 500]
"""
import socket
import sys
import time

TCC_IP        = '10.0.0.244'
DEFAULT_DUTY  = 80    # %
DURATION_SEC  = 10    # seconds to monitor
OVERCURRENT   = 500   # mA cutoff


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


def cmd(s: socket.socket, c: str, wait: float = 0.3) -> str:
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


def read_ina219(s: socket.socket, addr: int) -> tuple[int | None, float | None]:
    """Return (vbus_mv, current_ma) or (None, None) on read error."""
    vb = _parse_bytes(cmd(s, f'i2c read {addr:#x} 0x02 2', 0.3))
    ic = _parse_bytes(cmd(s, f'i2c read {addr:#x} 0x04 2', 0.3))
    if len(vb) < 2 or len(ic) < 2:
        return None, None
    vbus_mv = ((((vb[0] << 8) | vb[1]) >> 3) & 0x1FFF) * 4
    raw_cur = (ic[0] << 8) | ic[1]
    if raw_cur > 0x7FFF:
        raw_cur -= 0x10000
    current_ma = raw_cur * 10 / 1000.0
    return vbus_mv, current_ma


def main(duty: int = DEFAULT_DUTY,
         duration: float = DURATION_SEC,
         overcurrent: float = OVERCURRENT) -> None:
    s = connect()
    cmd(s, 'i2c init 15 16', 0.4)

    # Configure INA219 — continuous conversion, 32-sample average
    for addr in [0x40, 0x41]:
        cmd(s, f'i2c write {addr:#x} 0x05 0xa0 0x00', 0.2)  # CAL=0x1000
        cmd(s, f'i2c write {addr:#x} 0x00 0x21 0x9f', 0.2)  # config

    print(f'Setting VDUT → {duty}% ...')
    cmd(s, f'vdac set both {duty}', 0.5)
    time.sleep(1)

    print(f'\n{"duty":>5}  {"t(s)":>5}  {"V1(mV)":>8}  {"I1(mA)":>8}'
          f'  {"V2(mV)":>8}  {"I2(mA)":>8}')
    print('-' * 58)

    t0   = time.time()
    stop = 'time'

    while True:
        t = time.time() - t0
        if t >= duration:
            break
        v1, i1 = read_ina219(s, 0x40)
        v2, i2 = read_ina219(s, 0x41)
        if v1 is None:
            print(f'  read error at t={t:.2f}s')
            break
        print(f'{duty:>5}  {t:>5.2f}  {v1:>8}  {i1:>8.1f}  {v2:>8}  {i2:>8.1f}')
        if (abs(i1) > overcurrent or abs(i2) > overcurrent):
            stop = f'OVERCURRENT ({i1:.0f}/{i2:.0f} mA)'
            break
        time.sleep(0.5)

    cmd(s, 'vdac off', 0.5)
    s.close()
    print(f'\nVDUT off. Stopped: {stop}')


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='Monitor VDUT power delivery')
    p.add_argument('--duty',     type=int,   default=DEFAULT_DUTY)
    p.add_argument('--duration', type=float, default=DURATION_SEC)
    p.add_argument('--limit',    type=float, default=OVERCURRENT,
                   help='Overcurrent cutoff mA')
    args = p.parse_args()
    main(duty=args.duty, duration=args.duration, overcurrent=args.limit)

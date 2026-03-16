#!/usr/bin/env python3
"""
mux_scan.py — Scan all 4 TIE MUX banks via ADC128D818, tabulate mV per channel.

Issue: S3/S4 — TIE board connected; CH6/CH7 reassigned; MUX overvoltage
investigation; cut-channel verification after TIE rework.

Transport: TCP console 10.0.0.244:4242

MUX topology (TIE board):
  MUX0 (U3)  → ADC128D818 CH0 | addr GPIOs: A0=3  A1=4  A2=5  A3=6
  MUX1 (U11) → ADC128D818 CH1 | addr GPIOs: A0=7  A1=8  A2=9  A3=10
  MUX2 (U12) → ADC128D818 CH2 | addr GPIOs: A0=11 A1=12 A2=13 A3=14
  MUX3 (U1)  → ADC128D818 CH3 | addr GPIOs: A0=39 A1=40 A2=41 A3=42

I2C buses:
  INA219:   SDA=GPIO15 SCL=GPIO16
  ADC128:   SDA=GPIO16 SCL=GPIO15  (swapped — PCB layout)

Usage:
  python3 scripts/mux_scan.py [--duty 80] [--limit 300]
"""
import socket
import sys
import time

TCC_IP         = '10.0.0.244'
DEFAULT_DUTY   = 80    # VDUT PWM duty cycle %
CURRENT_LIMIT  = 300   # mA — abort if exceeded

MUX_CONFIG = [
    {'name': 'MUX0', 'ic': 'U3',  'adc_reg': 0x20, 'a': [3,  4,  5,  6 ]},
    {'name': 'MUX1', 'ic': 'U11', 'adc_reg': 0x21, 'a': [7,  8,  9,  10]},
    {'name': 'MUX2', 'ic': 'U12', 'adc_reg': 0x22, 'a': [11, 12, 13, 14]},
    {'name': 'MUX3', 'ic': 'U1',  'adc_reg': 0x23, 'a': [39, 40, 41, 42]},
]
ALL_MUX_GPIOS = [g for m in MUX_CONFIG for g in m['a']]


def connect() -> socket.socket:
    s = socket.socket()
    s.settimeout(10)
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
    s.settimeout(10)
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


def ina219_vbus(s: socket.socket, addr: int) -> int | None:
    out = cmd(s, f'i2c read {addr:#x} 0x02 2', 0.3)
    hv = _parse_bytes(out)
    if len(hv) < 2:
        return None
    raw = (hv[0] << 8) | hv[1]
    return ((raw >> 3) & 0x1FFF) * 4  # mV


def ina219_current_ma(s: socket.socket, addr: int) -> float | None:
    out = cmd(s, f'i2c read {addr:#x} 0x04 2', 0.3)
    hv = _parse_bytes(out)
    if len(hv) < 2:
        return None
    raw = (hv[0] << 8) | hv[1]
    if raw > 0x7FFF:
        raw -= 0x10000
    return raw * 10 / 1000.0  # mA


def adc128_ch_mv(s: socket.socket, reg: int) -> int | None:
    out = cmd(s, f'i2c read 0x1d {reg:#x} 2', 0.3)
    hv = _parse_bytes(out)
    if len(hv) < 2:
        return None
    raw = (hv[0] << 8) | hv[1]
    count = (raw >> 4) & 0xFFF
    return round(count * 2560 / 4096)  # mV (2.56V internal Vref)


def set_mux_addr(s: socket.socket, gpios: list[int], ch: int) -> None:
    for bit, gpio in enumerate(gpios):
        cmd(s, f'gpio set {gpio} {(ch >> bit) & 1}', 0.07)


def main(duty: int = DEFAULT_DUTY, current_limit: int = CURRENT_LIMIT) -> None:
    s = connect()

    # Configure all MUX address GPIOs as outputs, default low
    for gpio in ALL_MUX_GPIOS:
        cmd(s, f'gpio mode {gpio} out', 0.07)
        cmd(s, f'gpio set {gpio} 0',    0.07)

    # Apply VDUT power
    cmd(s, 'i2c init 15 16', 0.4)
    print(f'Setting VDUT → {duty}% ...')
    cmd(s, f'vdac set both {duty}', 0.5)
    print('Settling 3s ...')
    time.sleep(3)

    # Overcurrent check
    v1, i1 = ina219_vbus(s, 0x40), ina219_current_ma(s, 0x40)
    v2, i2 = ina219_vbus(s, 0x41), ina219_current_ma(s, 0x41)
    print(f'VDUT1: {v1} mV  {i1:.1f} mA    VDUT2: {v2} mV  {i2:.1f} mA')
    if any(c is not None and abs(c) > current_limit for c in [i1, i2]):
        print(f'OVERCURRENT > {current_limit} mA — aborting')
        cmd(s, 'vdac off', 0.5)
        s.close()
        sys.exit(1)

    # Init ADC128 (swapped bus, Mode 1)
    cmd(s, 'i2c init 16 15', 0.4)
    cmd(s, 'i2c write 0x1d 0x0b 0x02', 0.3)  # ADV_CFG: Mode 1
    cmd(s, 'i2c write 0x1d 0x00 0x01', 0.3)  # CONFIG: START
    time.sleep(0.15)

    # Scan all MUX banks
    for mux in MUX_CONFIG:
        print(f'\n{"="*50}')
        print(f'{mux["name"]} ({mux["ic"]})  ADC128 reg=0x{mux["adc_reg"]:02x}')
        print(f'  Addr GPIOs: A0={mux["a"][0]} A1={mux["a"][1]}'
              f' A2={mux["a"][2]} A3={mux["a"][3]}')
        print(f'{"Ch":>3}  {"mV":>6}   Note')
        print('-' * 35)
        for ch in range(16):
            cmd(s, 'i2c init 16 15', 0.15)
            set_mux_addr(s, mux['a'], ch)
            time.sleep(0.06)
            mv = adc128_ch_mv(s, mux['adc_reg'])
            cmd(s, 'i2c init 15 16', 0.15)
            note = ''
            if mv is None:   note = 'READ ERR'
            elif mv > 2400:  note = 'HIGH (>Vref)'
            elif mv < 20:    note = 'GND/float'
            print(f'  {ch:02d}  {mv if mv is not None else "---":>5} mV   {note}')

    cmd(s, 'vdac off', 0.5)
    s.close()
    print('\nDone.')


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='Scan all TIE MUX channels')
    p.add_argument('--duty',  type=int, default=DEFAULT_DUTY,
                   help=f'VDUT PWM duty %% (default {DEFAULT_DUTY})')
    p.add_argument('--limit', type=int, default=CURRENT_LIMIT,
                   help=f'Overcurrent limit mA (default {CURRENT_LIMIT})')
    args = p.parse_args()
    main(duty=args.duty, current_limit=args.limit)

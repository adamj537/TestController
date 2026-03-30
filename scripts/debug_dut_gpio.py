#!/usr/bin/env python3
"""
debug_dut_gpio.py — Interactive DUT GPIO debug shell.

Relays commands to the DUT via the TC console's 'uart cmd' passthrough.
Assumes the DUT is already powered (VDUT on, PB-A held) and PFW is running.
Run after a recipe run or use --power to let the TC power the DUT first.

Commands at the prompt:
  set  <pin>                     GPIO_SET <pin> HIGH  (OUTPUT HIGH)
  clr  <pin>                     GPIO_CLEAR <pin>     (OUTPUT LOW)
  rd   <pin>                     PIN_READ <pin>       (INPUT_PD then read)
  cfg  <pin> <mode>              GPIO_CONFIG <pin> <mode>
       modes: INPUT  INPUT_PU  INPUT_PD  OUTPUT_H  OUTPUT_L
  ina                            read INA219 #0 via TC selftest adc
  ver                            DUT firmware version
  enter                          send ENTER_TEST
  raw  <cmd>                     send any raw DUT UART command
  pins                           list all gpio_map pin names
  q / quit                       exit

Pin names (from DUT gpio_map):
  PA15 FLASHLIGHT_ENA   PC8  2611_SENSOR_ENA   PC5  VREF_ENA
  PD10 VIN#1_ENA(BR1)   PD11 VIN#2_ENA(BR2)   PB13 VIN#3_ENA(BR3)
  PD13 VIN#4_ENA(BR4)   PD14 VIN#5_ENA(KEEP)  PD2  VIN#6_ENA(BR6)
  PC9  TC_enable        PC13 PUMP_ENA          PA9  HEARTBEAT
  PE5  LED_B            PC7  LED_R             PB4  LED_G
  PA8  SS_ENA_A         PE9  TC_MODE           PD15 SS_ENA_B
  PA2  SOUNDER_carrier  PE1  SOUNDER_signal    PB3  SOUNDER_volume
  PD7  SOUNDER_gain1    PD9  SOUNDER_gain2     PE6  DISP_LED_ENA
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from tc_console import TcConsole

KNOWN_PINS = [
    'PA2', 'PA8', 'PA9', 'PA11', 'PA12', 'PA15',
    'PB3', 'PB4', 'PB13', 'PB15',
    'PC4', 'PC5', 'PC7', 'PC8', 'PC9', 'PC13',
    'PD1', 'PD2', 'PD7', 'PD9', 'PD10', 'PD11',
    'PD12', 'PD13', 'PD14', 'PD15',
    'PE1', 'PE3', 'PE5', 'PE6', 'PE7', 'PE8', 'PE9', 'PE13',
]


def dut(tc: TcConsole, cmd: str, wait: float = 0.8) -> str:
    """Send a raw DUT UART command via 'uart cmd' passthrough."""
    raw = tc.cmd(f'uart cmd {cmd}', wait=wait)
    # Strip TC prompt and echo lines, return just the DUT response line(s)
    lines = []
    for line in raw.splitlines():
        s = line.strip()
        if not s:
            continue
        if s.startswith('g3-tc|') or s.startswith(f'uart cmd {cmd}'):
            continue
        lines.append(s)
    return '\n'.join(lines) if lines else '(no response)'


def run_cmd(tc: TcConsole, line: str) -> bool:
    """Process one user command.  Return False to quit."""
    parts = line.strip().split()
    if not parts:
        return True
    cmd = parts[0].lower()

    if cmd in ('q', 'quit'):
        return False

    elif cmd == 'set':
        if len(parts) < 2:
            print('  Usage: set <pin>')
        else:
            print(' ', dut(tc, f'GPIO_SET {parts[1].upper()} HIGH'))

    elif cmd == 'clr':
        if len(parts) < 2:
            print('  Usage: clr <pin>')
        else:
            print(' ', dut(tc, f'GPIO_CLEAR {parts[1].upper()}'))

    elif cmd == 'rd':
        if len(parts) < 2:
            print('  Usage: rd <pin>')
        else:
            print(' ', dut(tc, f'PIN_READ {parts[1].upper()}'))

    elif cmd == 'cfg':
        if len(parts) < 3:
            print('  Usage: cfg <pin> <INPUT|INPUT_PU|INPUT_PD|OUTPUT_H|OUTPUT_L>')
        else:
            print(' ', dut(tc, f'GPIO_CONFIG {parts[1].upper()} {parts[2].upper()}'))

    elif cmd == 'ina':
        # Read INA219 via TC selftest
        r = tc.cmd('selftest adc', wait=2.0)
        for line in r.splitlines():
            s = line.strip()
            if s and not s.startswith('g3-tc|') and 'selftest adc' not in s:
                print(' ', s)

    elif cmd == 'ver':
        print(' ', dut(tc, 'VERSION', wait=0.5))

    elif cmd == 'enter':
        print(' ', dut(tc, 'ENTER_TEST'))

    elif cmd == 'raw':
        if len(parts) < 2:
            print('  Usage: raw <dut command>')
        else:
            cmd_str = ' '.join(parts[1:])
            wait = 4.0 if 'SCAN' in cmd_str.upper() else 1.0
            print(' ', dut(tc, cmd_str, wait=wait))

    elif cmd == 'pins':
        for i, p in enumerate(KNOWN_PINS):
            print(f'  {p:<8}', end='\n' if (i + 1) % 5 == 0 else '')
        if len(KNOWN_PINS) % 5:
            print()

    elif cmd in ('?', 'help'):
        print(__doc__)

    else:
        print(f'  Unknown: {cmd}  (try "?" for help)')

    return True


def main() -> None:
    print('Connecting to TC...')
    with TcConsole() as tc:
        print('Connected.\n')
        print('Commands: set/clr/rd/cfg <pin>  ina  ver  enter  raw <cmd>  pins  quit\n')

        # Auto-enter test mode — ignore ALREADY_IN_TEST
        r = dut(tc, 'ENTER_TEST')
        print(f'ENTER_TEST: {r}')
        print()

        while True:
            try:
                line = input('dut> ')
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not run_cmd(tc, line):
                break


if __name__ == '__main__':
    main()

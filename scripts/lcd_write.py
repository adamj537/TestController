#!/usr/bin/env python3
"""Write text to the TC's LCD1602 display via the console I2C commands.

Drives the HD44780 through the PCF8574 I2C backpack using the TC's
`i2c write` console command over the TCP net_console (port 4242).

Usage:
    python3 scripts/lcd_write.py "Hello World"              # line 1 only
    python3 scripts/lcd_write.py "Line 1 text" "Line 2 txt" # both lines
    python3 scripts/lcd_write.py --clear                     # clear display
    python3 scripts/lcd_write.py --init                      # re-init LCD
    python3 scripts/lcd_write.py --backlight off             # backlight off
    python3 scripts/lcd_write.py --backlight on              # backlight on
"""

import argparse
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

# PCF8574 I2C address (matches tc_hmi.c HMI_LCD_I2C_ADDR)
LCD_ADDR = 0x27

# PCF8574 bit positions (standard HD44780 backpack wiring)
LCD_RS = 1 << 0
LCD_RW = 1 << 1
LCD_EN = 1 << 2
LCD_BL = 1 << 3


class TcConsole:
    """Thin wrapper around the TC's TCP console."""

    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(5)
        self.sock.connect((host, port))
        time.sleep(0.3)
        try:
            self.sock.recv(4096)  # drain banner
        except socket.timeout:
            pass

    def cmd(self, command: str, wait: float = 0.5) -> str:
        self.sock.sendall((command + "\n").encode())
        time.sleep(wait)
        out = []
        self.sock.settimeout(0.5)
        try:
            while True:
                chunk = self.sock.recv(4096).decode(errors="replace")
                if not chunk:
                    break
                out.append(chunk)
        except socket.timeout:
            pass
        return "".join(out)

    def close(self) -> None:
        self.sock.close()


class Lcd1602:
    """Drive LCD1602 via PCF8574 using TC's `i2c write` console command."""

    def __init__(self, tc: TcConsole, addr: int = LCD_ADDR, backlight: bool = True) -> None:
        self.tc = tc
        self.addr = addr
        self.bl = LCD_BL if backlight else 0

    def _pcf_write(self, *bytes_to_send: int) -> None:
        """Write one or more bytes to PCF8574 via `i2c write`.

        `i2c write <addr> <reg> <b0> [b1 ...]` transmits reg+b0+b1...
        PCF8574 has no register concept — every received byte sets the
        port outputs.  So we use the first "reg" byte as actual data
        and append the rest as additional data bytes.
        """
        hex_args = " ".join(f"{b:02x}" for b in bytes_to_send)
        if len(bytes_to_send) == 1:
            # Need at least 2 args for i2c write (reg + 1 data byte).
            # Send the byte as reg, and repeat it as data — PCF8574
            # just latches the last byte received.
            self.tc.cmd(f"i2c write {self.addr:02x} {bytes_to_send[0]:02x} {bytes_to_send[0]:02x}", 0.05)
        else:
            first = bytes_to_send[0]
            rest = " ".join(f"{b:02x}" for b in bytes_to_send[1:])
            self.tc.cmd(f"i2c write {self.addr:02x} {first:02x} {rest}", 0.05)

    def _pulse_en(self, val: int) -> None:
        """Pulse EN: send val|EN then val&~EN in one I2C transaction."""
        self._pcf_write(val | LCD_EN, val & ~LCD_EN)

    def _write_nibble(self, nibble: int, rs: bool) -> None:
        val = (nibble << 4) | self.bl | (LCD_RS if rs else 0)
        self._pulse_en(val)

    def _write_byte(self, byte: int, rs: bool) -> None:
        self._write_nibble(byte >> 4, rs)
        self._write_nibble(byte & 0x0F, rs)

    def command(self, cmd: int) -> None:
        self._write_byte(cmd, rs=False)

    def data(self, ch: int) -> None:
        self._write_byte(ch, rs=True)

    def init(self) -> None:
        """HD44780 power-on init sequence (4-bit mode)."""
        time.sleep(0.05)
        self._write_nibble(0x03, rs=False)
        time.sleep(0.005)
        self._write_nibble(0x03, rs=False)
        time.sleep(0.001)
        self._write_nibble(0x03, rs=False)
        time.sleep(0.001)
        self._write_nibble(0x02, rs=False)  # switch to 4-bit mode

        self.command(0x28)  # Function set: 4-bit, 2 lines, 5x8
        self.command(0x0C)  # Display on, cursor off, blink off
        self.command(0x06)  # Entry mode: increment, no shift
        self.clear()

    def clear(self) -> None:
        self.command(0x01)
        time.sleep(0.002)

    def set_line(self, row: int, text: str) -> None:
        """Write up to 16 characters to row 0 (top) or 1 (bottom)."""
        addr = 0x80 if row == 0 else 0xC0
        self.command(addr)
        padded = text.ljust(16)[:16]
        for ch in padded:
            self.data(ord(ch))

    def set_backlight(self, on: bool) -> None:
        self.bl = LCD_BL if on else 0
        # Write current backlight state to PCF8574
        self._pcf_write(self.bl)


def main() -> None:
    parser = argparse.ArgumentParser(description="Write to TC LCD1602 display")
    parser.add_argument("line1", nargs="?", help="Text for line 1 (max 16 chars)")
    parser.add_argument("line2", nargs="?", help="Text for line 2 (max 16 chars)")
    parser.add_argument("--clear", action="store_true", help="Clear the display")
    parser.add_argument("--init", action="store_true", help="Re-initialize the LCD")
    parser.add_argument("--backlight", choices=["on", "off"], help="Control backlight")
    parser.add_argument("--host", default=HOST, help=f"TC IP (default: {HOST})")
    parser.add_argument("--port", type=int, default=PORT, help=f"TC port (default: {PORT})")
    args = parser.parse_args()

    if not any([args.line1, args.clear, args.init, args.backlight]):
        parser.print_help()
        sys.exit(1)

    tc = TcConsole(args.host, args.port)
    lcd = Lcd1602(tc)

    try:
        if args.init:
            print("Initializing LCD...")
            lcd.init()
            print("LCD initialized.")

        if args.backlight:
            on = args.backlight == "on"
            lcd.set_backlight(on)
            print(f"Backlight {'on' if on else 'off'}.")

        if args.clear:
            lcd.clear()
            print("Display cleared.")

        if args.line1 is not None:
            lcd.set_line(0, args.line1)
            print(f"Line 1: {args.line1[:16]}")

        if args.line2 is not None:
            lcd.set_line(1, args.line2)
            print(f"Line 2: {args.line2[:16]}")

    finally:
        tc.close()


if __name__ == "__main__":
    main()

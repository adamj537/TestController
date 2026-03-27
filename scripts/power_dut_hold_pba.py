#!/usr/bin/env python3
"""Power VDUT1 and hold PB-A asserted indefinitely.

Sequence:
  1. dut stop  — halt the DUT detect task (it owns SIG/ch3)
  2. wait 2 s  — let the detect task fully exit and call mux_release()
                 (new firmware: mux_release drives SIG HIGH, never floats)
  3. vdac_duty 1 80  — enable VDUT1 analogue rail (~4 V)
  4. mux select 0 0  — PB-A assert: ch0, SIG driven LOW (active-low)
                       No glitch: SIG is already OUTPUT from init; the
                       firmware drives LOW in one atomic gpio_set_level call.
  5. wait 500 ms  — 3V digital rail stabilises
  6. Read INA219 #0/#1 and print voltage + current
  7. Hold until Ctrl-C, then mux release + dut start

Press Ctrl-C to release PB-A and restore normal operation.
"""

import socket
import time
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(__file__))
from tc_config import HOST, PORT

# INA219 calibration: CURRENT_LSB = 10 µA/bit (matching firmware recipe)
INA219_CURRENT_LSB_UA: int = 10


def send_cmd(s: socket.socket, cmd: str, wait: float = 0.5) -> str:
    s.sendall((cmd + "\n").encode())
    time.sleep(wait)
    data = b""
    s.settimeout(2.0)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data.decode(errors="replace")


def _print_resp(cmd: str, resp: str) -> None:
    print(f">>> {cmd}")
    for line in resp.splitlines():
        if line.strip() and "g3-tc|" not in line:
            print(f"    {line}")


def _parse_i2c_read(resp: str) -> int:
    """Return raw 16-bit value from 'i2c read' response, or -1 on failure."""
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def _signed16(raw: int) -> int:
    """Interpret a 16-bit unsigned value as signed (two's complement)."""
    return raw - 0x10000 if raw > 0x7FFF else raw


def read_ina219(s: socket.socket, addr: int, label: str) -> None:
    """Configure INA219, then print bus voltage and current."""
    send_cmd(s, f"i2c write 0x{addr:02x} 0x00 0x21 0x9F")  # config reg
    send_cmd(s, f"i2c write 0x{addr:02x} 0x05 0xA0 0x00")  # calibration reg
    time.sleep(0.5)

    bus_raw = _parse_i2c_read(send_cmd(s, f"i2c read 0x{addr:02x} 0x02 2"))
    cur_raw = _parse_i2c_read(send_cmd(s, f"i2c read 0x{addr:02x} 0x04 2"))

    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    # Current register is signed 16-bit (two's complement)
    cur_ua = _signed16(cur_raw) * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1

    print(f"  {label}: {bus_mv} mV  {cur_ua / 1000:.1f} mA")


def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT} ...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.settimeout(3.0)
        # drain banner
        try:
            s.recv(4096)
        except socket.timeout:
            pass

        # Step 1 — stop DUT detect task
        resp = send_cmd(s, "dut stop")
        _print_resp("dut stop", resp)

        # Step 2 — wait for detect task to fully exit and release SIG
        # dut_detect_task calls mux_release() on exit; with new firmware this
        # drives SIG HIGH (not float).  Console 'dut stop' returns immediately
        # but the task is still running.  2 s covers the 1 s vTaskDelay plus
        # task teardown time.
        print("Waiting 2 s for DUT detect task to exit ...")
        time.sleep(2.0)

        # Step 3 — enable VDUT1 analogue rail
        resp = send_cmd(s, "vdac_duty 1 80")
        _print_resp("vdac_duty 1 80", resp)

        # Step 4 — assert PB-A (ch0, SIG=0)
        # SIG is always OUTPUT (new firmware); gpio_set_level drives LOW directly
        # — no glitch, no brief HIGH pulse.
        resp = send_cmd(s, "mux select 0 0")
        _print_resp("mux select 0 0", resp)

        # Step 5 — wait for 3V digital rail to stabilise
        print("\nWaiting 500 ms for 3V digital rail to stabilise ...")
        time.sleep(0.5)

        # Optional: read SWDIO to confirm DUT boot state
        resp = send_cmd(s, "gpio get 38")
        _print_resp("gpio get 38 (SWDIO)", resp)

        # Step 6 — INA219 power readings
        print("\nINA219 power readings with PB-A held:")
        read_ina219(s, 0x40, "VDUT1 (INA219 #0)")
        read_ina219(s, 0x41, "VDUT2 (INA219 #1)")

        # Step 7 — hold until Ctrl-C
        print("\nDUT powered, PB-A asserted.  Press Ctrl-C to release.")
        try:
            while True:
                time.sleep(5)
                # keepalive — prevent TC TCP idle timeout
                try:
                    s.sendall(b"\n")
                    s.recv(256)
                except Exception:
                    pass
        except KeyboardInterrupt:
            print("\nReleasing ...")
            # mux release: new firmware drives SIG HIGH (de-asserts PB-A)
            _print_resp("mux release", send_cmd(s, "mux release"))
            # restart DUT detect so normal operation resumes
            _print_resp("dut start", send_cmd(s, "dut start"))


if __name__ == "__main__":
    main()

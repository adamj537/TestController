#!/usr/bin/env python3
"""SWD flash with raw GPIO PB-A assertion — bypasses mux_select.

Powers DUT via raw GPIO (same approach as verify_pba_power.py),
then issues 'swd flash local nopwrcycle' so the TC connects to
the already-running target without its own power cycling.
"""
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

INA219_CURRENT_LSB_UA = 10
FLASH_TIMEOUT = 120


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


def pr(cmd: str, resp: str) -> None:
    print(f"  >>> {cmd}")
    for line in resp.splitlines():
        if line.strip() and "g3-tc|" not in line:
            print(f"      {line}")


def parse_i2c(resp: str) -> int:
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1


def read_ina(s: socket.socket, label: str) -> tuple[int, int]:
    send_cmd(s, "i2c write 0x40 0x00 0x21 0x9F", 0.3)
    send_cmd(s, "i2c write 0x40 0x05 0xA0 0x00", 0.3)
    time.sleep(0.5)
    bus_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x02 2", 0.5))
    cur_raw = parse_i2c(send_cmd(s, "i2c read 0x40 0x04 2", 0.5))
    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    print(f"  {label}: Bus={bus_mv} mV  Current={cur_ua / 1000:.1f} mA")
    return bus_mv, cur_ua


def main() -> int:
    target = "pfw"
    if len(sys.argv) > 1 and sys.argv[1] in ("pfw", "prod"):
        target = sys.argv[1]

    print(f"Connecting to TC at {HOST}:{PORT} ...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(3.0)
    try:
        s.recv(4096)
    except socket.timeout:
        pass

    # Step 1: stop DUT detect
    print("\n[1] Stop DUT detect")
    pr("dut stop", send_cmd(s, "dut stop", 1))
    time.sleep(2)

    # Step 2: configure MUX GPIOs as outputs
    print("\n[2] Configure MUX GPIOs")
    send_cmd(s, "gpio mode 0 out")
    send_cmd(s, "gpio mode 17 out")
    send_cmd(s, "gpio mode 18 out")
    send_cmd(s, "gpio mode 21 out")

    # Step 3: enable VDUT1
    print("\n[3] Enable VDUT1")
    pr("vdac_duty 1 80", send_cmd(s, "vdac_duty 1 80", 1))
    time.sleep(0.3)

    # Step 4: baseline INA219
    print("\n[4] Baseline INA219 (no PBA)")
    read_ina(s, "Baseline")

    # Step 5: assert PB-A via raw GPIO
    print("\n[5] Assert PB-A via raw GPIO")
    send_cmd(s, "gpio set 17 0")  # A0=0
    send_cmd(s, "gpio set 18 0")  # A1=0
    send_cmd(s, "gpio set 21 0")  # A2=0
    send_cmd(s, "gpio set 0 1")   # SIG HIGH first (clean entry to OUTPUT)
    time.sleep(0.1)
    send_cmd(s, "gpio set 0 0")   # SIG LOW — PB-A asserted
    print("  PB-A asserted")

    # Step 6: wait for boot
    print("\n[6] Waiting 1s for DUT boot ...")
    time.sleep(1.0)

    # Step 7: verify current
    print("\n[7] Post-PBA INA219")
    bus, cur = read_ina(s, "Post-PBA")
    if cur < 5000:
        print("  ERROR: DUT not drawing current — PBA assertion failed")
        send_cmd(s, "gpio set 0 1")
        send_cmd(s, "vdac off", 1)
        send_cmd(s, "dut start", 1)
        s.close()
        return 1

    # Step 8: SWD flash local nopwrcycle
    flash_cmd = f"swd flash local nopwrcycle --target {target}"
    print(f"\n[8] {flash_cmd}")
    s.settimeout(FLASH_TIMEOUT)
    s.sendall(f"{flash_cmd}\n".encode())

    buf = b""
    start = time.time()
    while time.time() - start < FLASH_TIMEOUT:
        try:
            s.settimeout(5)
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if b"[PASS]" in buf or b"[FAIL]" in buf:
                break
        except socket.timeout:
            continue

    elapsed = time.time() - start
    print(f"\n[{elapsed:.1f}s]")

    # Cleanup
    print("\n[9] Cleanup")
    send_cmd(s, "gpio set 0 1")   # release PBA
    send_cmd(s, "vdac off", 1)
    send_cmd(s, "dut start", 1)
    s.close()

    if b"[PASS]" in buf:
        print("SWD FLASH: PASS")
        return 0
    elif b"[FAIL]" in buf:
        print("SWD FLASH: FAIL")
        return 1
    else:
        print("SWD FLASH: no PASS/FAIL detected")
        return 1


if __name__ == "__main__":
    sys.exit(main())

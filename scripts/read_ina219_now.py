#!/usr/bin/env python3
"""One-shot INA219 read — does not disturb any existing GPIO/MUX state."""
import socket, time, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

INA219_CURRENT_LSB_UA = 10

def send_cmd(s, cmd, wait=0.3):
    s.sendall((cmd + "\n").encode())
    time.sleep(wait)
    data = b""
    s.settimeout(2.0)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            data += chunk
    except socket.timeout:
        pass
    return data.decode(errors="replace")

def parse_i2c(resp):
    for line in resp.splitlines():
        if "bytes]:" in line:
            parts = line.split("bytes]:")[1].strip().split()
            if len(parts) >= 2:
                return (int(parts[0], 16) << 8) | int(parts[1], 16)
    return -1

def signed16(v):
    return v - 0x10000 if v > 0x7FFF else v

def read_ina219(s, addr, label):
    send_cmd(s, f"i2c write 0x{addr:02x} 0x00 0x21 0x9F", wait=0.1)
    send_cmd(s, f"i2c write 0x{addr:02x} 0x05 0xA0 0x00", wait=0.1)
    time.sleep(0.5)
    bus_raw = parse_i2c(send_cmd(s, f"i2c read 0x{addr:02x} 0x02 2"))
    cur_raw = parse_i2c(send_cmd(s, f"i2c read 0x{addr:02x} 0x04 2"))
    sht_raw = parse_i2c(send_cmd(s, f"i2c read 0x{addr:02x} 0x01 2"))
    bus_mv = (bus_raw >> 3) * 4 if bus_raw >= 0 else -1
    cur_ua = cur_raw * INA219_CURRENT_LSB_UA if cur_raw >= 0 else -1
    sht_uv = sht_raw * 10 if sht_raw >= 0 else -1   # 10 µV/LSB
    print(f"  {label}: {bus_mv} mV  {cur_ua/1000:.1f} mA  shunt={sht_uv} uV")

with socket.socket() as s:
    s.connect((HOST, PORT))
    s.settimeout(3.0)
    try: s.recv(4096)
    except socket.timeout: pass
    read_ina219(s, 0x40, "VDUT1 (INA219 #0)")

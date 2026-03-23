#!/usr/bin/env python3
"""
ota_chunk_send.py — Send TC firmware as chunked OTA over LBB (USB-Serial-JTAG NDJSON channel).

Usage:
    python3 scripts/ota_chunk_send.py --port /dev/ttyUSB-ch1 --firmware .pio/build/esp32-Devkit/firmware.bin
    python3 scripts/ota_chunk_send.py --port /dev/ttyUSB-ch1 --firmware fw.bin --chunk-size 3072 --baud 115200

This is the flash path for multi-channel TCs enclosed in metal boxes where WiFi is not available.
The TC must have LBB enabled (mqtt lbb on + reboot) before running this script.
"""

import argparse
import base64
import hashlib
import json
import sys
import time
import serial

CHUNK_SIZE_DEFAULT = 3072  # bytes — base64 ~4096 chars; fits in LBB_RX_LINE_MAX=8192 with JSON overhead

def send_firmware(port: str, firmware_path: str, chunk_size: int, baud: int, timeout: float) -> bool:
    with open(firmware_path, "rb") as f:
        data = f.read()

    total = len(data)
    sha256 = hashlib.sha256(data).hexdigest()
    n_chunks = (total + chunk_size - 1) // chunk_size

    print(f"Firmware : {firmware_path}")
    print(f"Size     : {total:,} bytes ({n_chunks} chunks of {chunk_size}B)")
    print(f"SHA-256  : {sha256}")
    print(f"Port     : {port} @ {baud}")
    print()

    ser = serial.Serial(port, baud, timeout=1.0)
    time.sleep(0.5)  # let serial settle

    for seq, offset in enumerate(range(0, total, chunk_size)):
        chunk = data[offset:offset + chunk_size]
        encoded = base64.b64encode(chunk).decode("ascii")
        is_last = (offset + len(chunk)) >= total

        dcmd: dict = {
            "cmd": "ota_chunk",
            "seq": seq,
            "offset": offset,
            "data": encoded,
            "total": total,
        }
        if is_last:
            dcmd["sha256"] = sha256

        line = json.dumps(dcmd, separators=(",", ":")) + "\n"
        ser.write(line.encode("utf-8"))
        ser.flush()

        pct = min(100, (offset + len(chunk)) * 100 // total)
        print(f"\r  Sending chunk {seq + 1}/{n_chunks}  [{pct:3d}%]", end="", flush=True)

        # Drain any NDJSON responses from TC (ota_progress lines)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                break
            try:
                msg = json.loads(raw.decode("utf-8", errors="replace").strip())
                payload = msg.get("p", {})
                status = payload.get("status", "")
                if status in ("error", "failed"):
                    print(f"\nTC error: {payload.get('error_code')} — {payload.get('error_msg')}")
                    ser.close()
                    return False
                if status == "complete":
                    print(f"\n  OTA complete — TC rebooting")
                    # Wait for NBIRTH on new firmware
                    _wait_for_nbirth(ser, timeout=120)
                    ser.close()
                    return True
            except (json.JSONDecodeError, UnicodeDecodeError):
                pass  # IDF log line or partial — skip
            break  # one response per chunk is enough; continue sending

    print()  # newline after progress
    print("All chunks sent — waiting for TC to validate and reboot...")
    _wait_for_nbirth(ser, timeout=120)
    ser.close()
    return True


def _wait_for_nbirth(ser: serial.Serial, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        try:
            msg = json.loads(raw.decode("utf-8", errors="replace").strip())
            if msg.get("t") == "NBIRTH":
                p = msg.get("p", {})
                version = p.get("Versions/tc", "?")
                hw_uid = p.get("Properties/HwUid", "?")
                fixture_id = p.get("Properties/FixtureId", "")
                print(f"NBIRTH received — version={version}  hw_uid={hw_uid}  fixture_id={fixture_id or '(unprovisioned)'}")
                return
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass
    print("WARNING: no NBIRTH received within timeout — check TC manually")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyUSB-ch1")
    parser.add_argument("--firmware", required=True, help="Path to firmware.bin")
    parser.add_argument("--chunk-size", type=int, default=CHUNK_SIZE_DEFAULT, help=f"Chunk size in bytes (default {CHUNK_SIZE_DEFAULT})")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument("--chunk-timeout", type=float, default=2.0, help="Seconds to wait for TC ack per chunk (default 2.0)")
    args = parser.parse_args()

    ok = send_firmware(args.port, args.firmware, args.chunk_size, args.baud, args.chunk_timeout)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

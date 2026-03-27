#!/usr/bin/env python3
"""decode_otadata.py — Decode ESP32 OTA data partition binary.

Reads the otadata partition (8KB at 0xf000) and prints which OTA slot is
active and what state each slot is in.

Usage:
    python3 scripts/decode_otadata.py /tmp/otadata.bin

Capture from device:
    python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \\
        --port /dev/ttyACM0 read_flash 0xf000 0x2000 /tmp/otadata.bin
"""
import sys
import struct

# esp_ota_img_states_t (ESP-IDF 5.x, simple integer enum)
OTA_STATES = {
    0x00000000: "NEW",
    0x00000001: "PENDING_VERIFY",
    0x00000002: "VALID",
    0x00000003: "INVALID",
    0x00000004: "ABORTED",
    0xFFFFFFFF: "UNDEFINED (erased)",
}

# OTA data record layout: 32 bytes
# ota_seq(4) + label(20) + ota_state(4) + crc(4)


def decode_record(data: bytes, offset: int) -> dict:
    seq = struct.unpack_from("<I", data, offset)[0]
    label = data[offset + 4:offset + 24].rstrip(b"\x00").decode("ascii", errors="replace")
    ota_state = struct.unpack_from("<I", data, offset + 24)[0]
    crc = struct.unpack_from("<I", data, offset + 28)[0]
    return {"seq": seq, "label": label, "ota_state": ota_state, "crc": crc}


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/otadata.bin"
    with open(path, "rb") as f:
        data = f.read()

    records = [decode_record(data, 0), decode_record(data, 4096)]

    active_seq = 0
    for i, r in enumerate(records):
        seq = r["seq"]
        state_str = OTA_STATES.get(r["ota_state"], f"0x{r['ota_state']:08X}")
        if seq == 0xFFFFFFFF:
            print(f"Record {i}: empty (never written)")
        else:
            slot = (seq - 1) % 2
            print(f"Record {i}: seq={seq} -> ota_{slot}  state={state_str}  crc=0x{r['crc']:08X}")
            if seq > active_seq:
                active_seq = seq

    print()
    if active_seq == 0:
        print("Active: factory (otadata empty — no OTA ever performed)")
    else:
        active_slot = (active_seq - 1) % 2
        print(f"Active boot target: ota_{active_slot} (highest seq={active_seq})")
        print()
        print("To revert to factory boot after USB flash:")
        print("  python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \\")
        print("      --port /dev/ttyACM0 erase_region 0xf000 0x2000")


if __name__ == "__main__":
    main()

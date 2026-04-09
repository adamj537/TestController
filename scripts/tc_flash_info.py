#!/usr/bin/env python3
"""tc_flash_info.py — Read TC device config and firmware versions directly from flash.

Reads NVS config (WiFi credentials, MQTT broker, device serial, etc.) and the
app descriptor from each firmware slot without booting the application.

Requires the device to be in ROM download mode — hold the BOOT button while
power-cycling, or use a UART bridge that supports hardware reset strapping.

Usage:
    python3 scripts/tc_flash_info.py [--port /dev/ttyUSB0]
"""
from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


# ── Tool discovery ────────────────────────────────────────────────────────────

def find_esptool() -> str:
    """Locate esptool.py in PlatformIO packages or PATH."""
    pio_path = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(pio_path):
        return pio_path
    return "esptool.py"


def find_nvs_tool() -> str:
    candidates = [
        os.path.expanduser(
            "~/.platformio/packages/framework-espidf/components"
            "/nvs_flash/nvs_partition_tool/nvs_tool.py"
        ),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    raise FileNotFoundError(
        "nvs_tool.py not found. Install ESP-IDF PlatformIO framework package."
    )


# ── Partition map (partitions_ota_32mb.csv) ───────────────────────────────────

NVS_OFFSET      = 0x9000
NVS_SIZE        = 0x6000
OTADATA_OFFSET  = 0xF000
OTADATA_SIZE    = 0x2000
FACTORY_OFFSET  = 0x20000
OTA_0_OFFSET    = 0x220000
OTA_1_OFFSET    = 0x420000
APP_HEADER_READ = 512   # enough to locate esp_app_desc_t in any slot


# ── esp_app_desc_t parsing ────────────────────────────────────────────────────

APP_DESC_MAGIC   = 0xABCD5432  # ESP-IDF 5.x (older IDF used 0xABCD5AA5)
OFF_VERSION      = 16   # char[32]
OFF_PROJECT_NAME = 48   # char[32]
OFF_TIME         = 80   # char[16]
OFF_DATE         = 96   # char[16]
OFF_IDF_VER      = 112  # char[32]


def _cstr(b: bytes) -> str:
    return b.rstrip(b"\x00").decode("utf-8", errors="replace")


def parse_app_desc(data: bytes, slot_name: str) -> None:
    magic = APP_DESC_MAGIC.to_bytes(4, "little")
    idx = data.find(magic)
    if idx == -1:
        print(f"  {slot_name:<10} erased / no app present")
        return
    version      = _cstr(data[idx + OFF_VERSION      : idx + OFF_VERSION      + 32])
    project_name = _cstr(data[idx + OFF_PROJECT_NAME : idx + OFF_PROJECT_NAME + 32])
    build_date   = _cstr(data[idx + OFF_DATE         : idx + OFF_DATE         + 16])
    build_time   = _cstr(data[idx + OFF_TIME         : idx + OFF_TIME         + 16])
    idf_ver      = _cstr(data[idx + OFF_IDF_VER      : idx + OFF_IDF_VER      + 32])
    print(f"  {slot_name:<10} {version}  "
          f"({project_name}, {build_date} {build_time}, IDF {idf_ver})")


# ── OTA data decoding (replicates decode_otadata.py logic) ───────────────────

OTA_STATES: dict[int, str] = {
    0x00000000: "NEW",
    0x00000001: "PENDING_VERIFY",
    0x00000002: "VALID",
    0x00000003: "INVALID",
    0x00000004: "ABORTED",
    0xFFFFFFFF: "UNDEFINED (erased)",
}


def decode_otadata(data: bytes) -> None:
    active_seq = 0
    for i in range(2):
        off = i * 4096
        seq       = struct.unpack_from("<I", data, off)[0]
        ota_state = struct.unpack_from("<I", data, off + 24)[0]
        state_str = OTA_STATES.get(ota_state, f"0x{ota_state:08X}")
        if seq == 0xFFFFFFFF:
            print(f"  record[{i}]  empty (never written)")
        else:
            slot = (seq - 1) % 2
            print(f"  record[{i}]  seq={seq} → ota_{slot}  state={state_str}")
            if seq > active_seq:
                active_seq = seq
    print()
    if active_seq == 0:
        print("  Active boot target: factory (otadata empty)")
    else:
        print(f"  Active boot target: ota_{(active_seq - 1) % 2}  (seq={active_seq})")


# ── esptool flash reads ───────────────────────────────────────────────────────

def read_flash(esptool: str, port: str, offset: int, size: int,
               reset_after: bool = False) -> bytes:
    after = "hard_reset" if reset_after else "no_reset"
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        tmp = f.name
    try:
        result = subprocess.run(
            [sys.executable, esptool,
             "--port", port, "--chip", "esp32s3", "--no-stub",
             "--after", after,
             "read_flash", hex(offset), hex(size), tmp],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            raise RuntimeError(
                f"esptool read_flash failed at {hex(offset)} size {hex(size)}"
            )
        return Path(tmp).read_bytes()
    finally:
        Path(tmp).unlink(missing_ok=True)


# ── NVS display ───────────────────────────────────────────────────────────────

NVS_NAMESPACES = ("wifi_cfg:", "mqtt:", "device:", "autostart:", "provision:", "recipes:")


def show_nvs(esptool: str, nvs_tool: str, port: str) -> None:
    print("── NVS config ───────────────────────────────────────────────────────────────")
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        tmp = f.name
    try:
        result = subprocess.run(
            [sys.executable, esptool,
             "--port", port, "--chip", "esp32s3", "--no-stub",
             "--after", "no_reset",
             "read_flash", hex(NVS_OFFSET), hex(NVS_SIZE), tmp],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            print("  (failed to read NVS partition)")
            return

        parse = subprocess.run(
            [sys.executable, nvs_tool, "-d", "minimal", tmp],
            capture_output=True, text=True,
        )
        if parse.returncode != 0:
            print(f"  (NVS parse failed: {parse.stderr.strip()})")
            return

        printed = False
        for line in parse.stdout.splitlines():
            stripped = line.strip()
            if any(stripped.startswith(ns) for ns in NVS_NAMESPACES):
                print(f"  {stripped}")
                printed = True
        if not printed:
            print("  (no matching keys found — NVS may be erased)")
    finally:
        Path(tmp).unlink(missing_ok=True)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyUSB0",
                        help="Serial port for UART bridge (default: /dev/ttyUSB0)")
    parser.add_argument("--reset", action="store_true",
                        help="Hard-reset the device after reading (boots normally)")
    args = parser.parse_args()

    esptool = find_esptool()
    nvs_tool = find_nvs_tool()

    print(f"Reading flash via {args.port} ...\n")

    show_nvs(esptool, nvs_tool, args.port)

    print("\n── OTA data ─────────────────────────────────────────────────────────────────")
    otadata = read_flash(esptool, args.port, OTADATA_OFFSET, OTADATA_SIZE)
    decode_otadata(otadata)

    print("\n── Firmware slots ───────────────────────────────────────────────────────────")
    slots = [
        ("factory",  FACTORY_OFFSET),
        ("ota_0",    OTA_0_OFFSET),
        ("ota_1",    OTA_1_OFFSET),
    ]
    for i, (name, offset) in enumerate(slots):
        is_last = (i == len(slots) - 1)
        data = read_flash(esptool, args.port, offset, APP_HEADER_READ,
                          reset_after=(is_last and args.reset))
        parse_app_desc(data, name)

    if not args.reset:
        print("\n  (device remains in download mode; use --reset to boot normally)")
    print()


if __name__ == "__main__":
    main()

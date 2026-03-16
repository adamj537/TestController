#!/bin/bash
# esptool_full_flash.sh — Full clean flash: bootloader + partitions + ota_data + firmware.
#
# Issue: S7 — Used when OTA is unavailable (e.g. after PSRAM/sdkconfig changes that
# break the existing flash, or when bringing up a blank board).
#
# Includes: bootloader (0x0000), partition table (0x8000), ota_data (0xF000),
#           application firmware (0x20000).
#
# Usage:
#   ./scripts/esptool_full_flash.sh                    # default: /dev/ttyUSB0, no-reset
#   ./scripts/esptool_full_flash.sh /dev/ttyACM0       # USB CDC port
#   ./scripts/esptool_full_flash.sh /dev/ttyUSB0 auto  # auto-reset (board in normal mode)
#
# Note: 'no_reset' mode is needed when the device is already in ROM download mode
# (hold BOOT + press RST, then release RST, then release BOOT).
# Use 'auto' (default_reset) for normal operation when auto-reset is available.

set -e

PORT="${1:-/dev/ttyUSB0}"
RESET_MODE="${2:-no_reset}"

if [ "$RESET_MODE" = "auto" ]; then
    BEFORE="default_reset"
else
    BEFORE="no_reset"
fi

ENV="esp32-Devkit"
BUILD_DIR=".pio/build/${ENV}"

if [ ! -f "${BUILD_DIR}/firmware.bin" ]; then
    echo "ERROR: ${BUILD_DIR}/firmware.bin not found — run: pio run -e ${ENV}"
    exit 1
fi

# Print binary header info for sanity check
echo "--- firmware.bin header ---"
python3 -c "
with open('${BUILD_DIR}/firmware.bin', 'rb') as f:
    hdr = f.read(8)
modes = {0:'QIO', 1:'QOUT', 2:'DIO', 3:'DOUT', 0xFF:'KEEP'}
print(f'  Magic: 0x{hdr[0]:02X}')
print(f'  Flash mode: 0x{hdr[2]:02X} = {modes.get(hdr[2], \"UNKNOWN\")}')
print(f'  Flash speed+size: 0x{hdr[3]:02X}')
"
echo ""

echo "Flashing ${BUILD_DIR}/ to ${PORT} (before=${BEFORE}) ..."
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 \
    --port "${PORT}" \
    --baud 460800 \
    --before "${BEFORE}" \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 16MB \
    0x0000  "${BUILD_DIR}/bootloader.bin" \
    0x8000  "${BUILD_DIR}/partitions.bin" \
    0xf000  "${BUILD_DIR}/ota_data_initial.bin" \
    0x20000 "${BUILD_DIR}/firmware.bin"

echo ""
echo "Flash complete. Waiting for TCP console..."
python3 scripts/wait_for_tcp.py

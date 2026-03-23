#!/bin/bash
# serve_firmware.sh — Serve PlatformIO build output over HTTP for OTA.
#
# Issue: S1 — OTA WiFi bypass; used throughout all sessions to serve
# firmware.bin so the ESP32 can fetch it via 'ota update http://...'.
#
# Usage:
#   ./scripts/serve_firmware.sh [env]     # default env: esp32-Devkit
#   ./scripts/serve_firmware.sh g3-dut    # serve DUT firmware instead
#
# After starting, OTA from TCP console:
#   ota update http://<WSL-IP>:8080/firmware.bin
#
# WSL IP: ip addr show eth0 | grep 'inet '

set -e

ENV="${1:-esp32-Devkit}"
BUILD_DIR=".pio/build/${ENV}"

if [ ! -f "${BUILD_DIR}/firmware.bin" ]; then
    echo "ERROR: ${BUILD_DIR}/firmware.bin not found"
    echo "Run: pio run -e ${ENV}"
    exit 1
fi

# Kill any existing server on 8080
fuser -k 8080/tcp 2>/dev/null || true
sleep 0.3

echo "Serving ${BUILD_DIR}/ on port 8080 ..."
echo "OTA command: ota update http://$(hostname -I | awk '{print $1}'):8080/firmware.bin"
echo "Press Ctrl-C to stop."
echo ""
python3 -m http.server 8080 --directory "${BUILD_DIR}"

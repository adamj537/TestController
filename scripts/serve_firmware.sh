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

# Pick the IP that routes to the TC subnet (handles multi-interface WSL setups)
TC_IP=$(python3 -c "
try:
    from local_config import TC_IP
    print(TC_IP)
except Exception:
    print('192.168.50.30')
" 2>/dev/null || echo "192.168.50.30")
WSL_IP=$(ip route get "${TC_IP}" 2>/dev/null | awk 'NR==1 {for(i=1;i<=NF;i++) if($i=="src") {print $(i+1); exit}}')
WSL_IP="${WSL_IP:-$(hostname -I | awk '{print $1}')}"

echo "Serving ${BUILD_DIR}/ on port 8080 ..."
echo "OTA command: ota update http://${WSL_IP}:8080/firmware.bin"
echo "Press Ctrl-C to stop."
echo ""
python3 -m http.server 8080 --directory "${BUILD_DIR}"

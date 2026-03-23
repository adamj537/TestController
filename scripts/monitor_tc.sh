#!/bin/bash
# monitor_tc.sh — Stream TC console output until SM reaches a terminal state.
#
# Issue: All sessions — watch live test traffic (start → Precheck → PreGate →
# Testing/Fail → Idle) from the TCC TCP console.
#
# Usage:
#   ./scripts/monitor_tc.sh          # monitor at 10.0.0.244 (default)
#   ./scripts/monitor_tc.sh <ip>     # override TC IP
#
# Exits when the state machine reaches Idle, Pass, or Fail, or on Ctrl-C.

set -e

TC_IP="${1:-10.0.0.244}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TCP_CONSOLE="$(realpath "${SCRIPT_DIR}/../../../../scripts/tcp_console.py" 2>/dev/null || echo "")"

if [ -z "$TCP_CONSOLE" ] || [ ! -f "$TCP_CONSOLE" ]; then
    # Fallback: search common locations
    for candidate in \
        "/home/cbasta/G3-MB-Tester/scripts/tcp_console.py" \
        "${SCRIPT_DIR}/../../../scripts/tcp_console.py"; do
        if [ -f "$candidate" ]; then
            TCP_CONSOLE="$candidate"
            break
        fi
    done
fi

if [ -z "$TCP_CONSOLE" ] || [ ! -f "$TCP_CONSOLE" ]; then
    echo "ERROR: tcp_console.py not found"
    exit 1
fi

echo "Monitoring TC at ${TC_IP} (Ctrl-C to stop)..."
python3 "$TCP_CONSOLE" --host "$TC_IP" monitor

#!/usr/bin/env python3
"""watch_ota_boot.py — Trigger OTA, wait for reboot, capture full boot window.

Sends 'ota update <url>' to the TC TCP console, waits for the connection to
drop (reboot), then reconnects and streams all output for OTA_WATCH_S seconds.
This captures the full OTA health check window (90s) plus buffer so we can see
the mDNS resolution attempt, MQTT connect log, and OTA health pass/fail.

Key log lines to look for:
  [tc_mqtt] mDNS: fixtureops-broker → <ip>    (mDNS resolved, used this IP)
  [tc_mqtt] mDNS: fixtureops-broker not found  (mDNS timed out, using NVS URL)
  [tc_mqtt] init  broker=...                   (what broker URL was loaded from NVS)
  [g3-tc]   OTA health: PASSED                 (firmware validated)
  [g3-tc]   OTA health: FAILED                 (wifi=%d mqtt=%d, rolling back)

Usage:
    python3 scripts/watch_ota_boot.py <ota_url>
    python3 scripts/watch_ota_boot.py http://192.168.50.41:8080/firmware.bin
    python3 scripts/watch_ota_boot.py --no-trigger   # skip OTA cmd, just watch
"""

import argparse
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT

OTA_WATCH_S   = 100   # seconds to capture after reboot (> 90s health check)
RECONNECT_S   = 60    # max seconds to wait for TCP console to come back
OTA_TIMEOUT_S = 120   # max seconds to wait for OTA download + reboot

# Lines containing these strings are highlighted in the output
HIGHLIGHT = [
    "mDNS",
    "mqtt_connect",
    "broker_attempt",
    "OTA health",
    "no broker",
    "broker=",
    "WiFi connected",
    "mqtt://",
    "MQTT error",
    "rolledback",
    "validated",
    "PENDING VERIFY",
]


def _tc_connect(ip: str, port: int, timeout: float = 10.0) -> socket.socket:
    s = socket.socket()
    s.settimeout(timeout)
    s.connect((ip, port))
    time.sleep(0.3)
    # drain banner / prompt
    s.settimeout(0.3)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
    s.settimeout(timeout)
    return s


def _reconnect_with_retry(ip: str, port: int, timeout: float) -> socket.socket | None:
    """Try to reconnect to the TC console within timeout seconds."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket()
            s.settimeout(2.0)
            s.connect((ip, port))
            time.sleep(0.2)
            # drain any banner
            s.settimeout(0.3)
            try:
                while s.recv(4096):
                    pass
            except OSError:
                pass
            return s
        except OSError:
            time.sleep(0.5)
    return None


def _send_ota_and_wait_for_reboot(ip: str, port: int, ota_url: str,
                                   timeout: float) -> bool:
    """Send OTA command and watch for the TC's reboot signal.

    The TC prints exactly 'OTA complete — rebooting in 1 second' (cmd_ota.c)
    just before calling esp_restart().  We watch for that string.

    The TCP connection may drop briefly during the flash write (WiFi
    starvation) — NOT the real reboot.  On spurious drops, we reconnect
    and keep watching.  Returns True when reboot signal is seen.
    """
    REBOOT_SIGNAL = "rebooting in 1 second"
    deadline = time.time() + timeout

    s = _tc_connect(ip, port)
    s.sendall((f"ota update {ota_url}\n").encode())
    s.settimeout(1.0)

    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                for line in chunk.splitlines():
                    print(f"[OTA] {line}", flush=True)
                if REBOOT_SIGNAL in chunk:
                    print("[OTA] reboot signal detected — waiting 3s for restart ...",
                          flush=True)
                    s.close()
                    time.sleep(3.0)
                    return True
        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError,
                OSError):
            print("[OTA] TCP drop during flash write — reconnecting ...", flush=True)
            try:
                s.close()
            except OSError:
                pass
            time.sleep(2.0)
            reconnected = _reconnect_with_retry(ip, port, 15.0)
            if reconnected is None:
                print("[OTA] could not reconnect — TC may have rebooted already",
                      flush=True)
                return False
            s = reconnected
        except socket.timeout:
            continue

    try:
        s.close()
    except OSError:
        pass
    return False


def _wait_for_tcp_console(ip: str, port: int, timeout: float) -> socket.socket | None:
    """Poll until the TCP console accepts connections. Returns socket or None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket()
            s.settimeout(2.0)
            s.connect((ip, port))
            # drain any boot banner
            time.sleep(0.3)
            s.settimeout(0.3)
            try:
                while s.recv(4096):
                    pass
            except OSError:
                pass
            return s
        except OSError:
            time.sleep(0.5)
    return None


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("ota_url", nargs="?", default=None,
                   help="OTA firmware URL (omit with --no-trigger)")
    p.add_argument("--tc-ip",     default=HOST)
    p.add_argument("--tc-port",   type=int, default=PORT)
    p.add_argument("--no-trigger", action="store_true",
                   help="Skip OTA command — just watch console (TC already rebooting)")
    args = p.parse_args()

    if not args.no_trigger and not args.ota_url:
        print("ERROR: provide ota_url or --no-trigger", file=sys.stderr)
        sys.exit(1)

    if not args.no_trigger:
        print(f"[*] Triggering OTA + watching for reboot signal (up to {OTA_TIMEOUT_S}s) ...")
        dropped = _send_ota_and_wait_for_reboot(
            args.tc_ip, args.tc_port, args.ota_url, OTA_TIMEOUT_S)
        if not dropped:
            print("WARNING: reboot signal not seen — TC may not have rebooted cleanly")
    else:
        print(f"[*] --no-trigger: skipping OTA command, waiting for TCP console ...")

    print(f"[*] Waiting for TCP console to come back (up to {RECONNECT_S}s) ...")
    t_reboot = time.time()
    s2 = _wait_for_tcp_console(args.tc_ip, args.tc_port, RECONNECT_S)
    if s2 is None:
        print(f"ERROR: TCP console did not come back within {RECONNECT_S}s", file=sys.stderr)
        sys.exit(1)

    t_connected = time.time()
    print(f"[*] TCP console back after {t_connected - t_reboot:.1f}s")
    print("=" * 70)
    print(f"  Capturing {OTA_WATCH_S}s of boot output (OTA health check window)")
    print("=" * 70)

    s2.settimeout(1.0)
    deadline = time.time() + OTA_WATCH_S
    buf: list[str] = []

    while time.time() < deadline:
        try:
            chunk = s2.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                buf.append(chunk)
                # Print with highlight markers for key lines
                for line in chunk.splitlines():
                    ts = f"[{time.time() - t_connected:6.1f}s]"
                    tag = " *** " if any(h in line for h in HIGHLIGHT) else "     "
                    print(f"{ts}{tag}{line}")
                sys.stdout.flush()
        except socket.timeout:
            continue
        except OSError:
            print("\n[*] TCP connection dropped (OTA rollback reboot?)")
            break

    s2.close()

    print("\n" + "=" * 70)
    full = "".join(buf)
    if "OTA health: PASSED" in full:
        print("  OTA RESULT: PASSED — firmware validated")
    elif "OTA health: FAILED" in full or "rolling back" in full.lower():
        print("  OTA RESULT: FAILED — rollback triggered")
    elif "already validated" in full:
        print("  OTA RESULT: partition already validated (no pending verify)")
    else:
        print("  OTA RESULT: unknown (check output above)")
    print("=" * 70)


if __name__ == "__main__":
    main()

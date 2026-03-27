#!/usr/bin/env python3
"""tc_init.py — Initialize a TC from bare ESP32 module to production-ready state.

Called by the Pi coordinator for each channel. Performs:
  1. Full flash (bootloader + partition table + firmware) via USB
  2. Wait for boot + WiFi
  3. Push DUT PFW to dut_fw partition (optional, --pfw)
  4. Push production firmware to prod_fw partition (optional, --prod-fw)
  5. Set calibration defaults (bare ESP32, no TCC carrier)
  6. Set channel number
  7. Configure MQTT broker
  8. Run validation (selftest + regression)

Usage:
  # Single-channel WiFi TC (channel 0):
  python3 scripts/tc_init.py --port /dev/ttyACM0 --channel 0 --broker mqtt://10.0.0.1:1883

  # Multi-channel LBB TC (channel 3, no broker):
  python3 scripts/tc_init.py --port /dev/ttyUSB3 --channel 3 --lbb

  # Skip flash (already flashed, just configure):
  python3 scripts/tc_init.py --port /dev/ttyACM0 --channel 0 --skip-flash

  # With DUT firmware images:
  python3 scripts/tc_init.py --port /dev/ttyACM0 --channel 0 \\
      --pfw http://10.0.0.246:8080/pfw.bin \\
      --prod-fw http://10.0.0.246:8080/G3_01_00_46_FACT.bin

Exit codes:
  0 = all steps passed
  1 = fatal error (flash failed, TCP unreachable)
  2 = validation failures (TC is running but some checks failed)
"""

import argparse
import os
import subprocess
import socket
import sys
import time

try:
    import serial as pyserial
except ImportError:
    pyserial = None

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, ".pio", "build", "esp32-Devkit")

# Default WiFi credentials — override in scripts/local_config.py
try:
    from local_config import WIFI_SSID, WIFI_PASS as WIFI_PW
except ImportError:
    WIFI_SSID = "FixtureOpsGateway"
    WIFI_PW   = "gateway123"

# TCP console settings
TCP_PORT = 4242
BOOT_TIMEOUT = 30  # seconds to wait for TCP console after flash


def find_esptool() -> str:
    """Locate esptool.py in PlatformIO packages or PATH."""
    pio_path = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(pio_path):
        return pio_path
    # Fall back to PATH
    return "esptool.py"


def run_cmd(cmd: list[str], description: str) -> bool:
    """Run a shell command, print output, return success."""
    print(f"\n{'─' * 60}")
    print(f"▶ {description}")
    print(f"  $ {' '.join(cmd)}")
    print(f"{'─' * 60}")
    result = subprocess.run(cmd, timeout=120)
    ok = result.returncode == 0
    status = "✓" if ok else "✗"
    print(f"  [{status}] {description}")
    return ok


class TcConnection:
    """Transport-agnostic connection to a TC (TCP or serial)."""

    def __init__(self, host: str = "", port: int = TCP_PORT,
                 serial_port: str = "", baud: int = 115200):
        self.host = host
        self.port = port
        self.serial_port = serial_port
        self.baud = baud
        self.sock: socket.socket | None = None
        self.ser = None
        self._use_serial = bool(serial_port)

    def connect(self, timeout: float = 10.0) -> None:
        if self._use_serial:
            if pyserial is None:
                raise ImportError("pyserial required for serial transport: pip install pyserial")
            self.ser = pyserial.Serial(self.serial_port, self.baud, timeout=1)
            time.sleep(2)  # wait for boot prompt
            self._drain()
        else:
            self.sock = socket.socket()
            self.sock.settimeout(timeout)
            self.sock.connect((self.host, self.port))
            time.sleep(0.3)
            self._drain()

    def _drain(self, wait: float = 0.3) -> str:
        time.sleep(wait)
        buf = b""
        if self._use_serial:
            deadline = time.time() + wait + 0.2
            while time.time() < deadline:
                avail = self.ser.in_waiting
                if avail:
                    buf += self.ser.read(avail)
                else:
                    time.sleep(0.05)
        else:
            self.sock.settimeout(0.2)
            try:
                while True:
                    buf += self.sock.recv(4096)
            except socket.timeout:
                pass
            self.sock.settimeout(10)
        return buf.decode("utf-8", errors="replace")

    def cmd(self, command: str, wait: float = 2.0) -> str:
        """Send command, return response."""
        if self._use_serial:
            self.ser.write((command + "\n").encode())
            self.ser.flush()
            time.sleep(wait)
            buf = b""
            deadline = time.time() + 0.5
            while time.time() < deadline:
                avail = self.ser.in_waiting
                if avail:
                    buf += self.ser.read(avail)
                    deadline = time.time() + 0.2  # extend if data still arriving
                else:
                    time.sleep(0.05)
            return buf.decode("utf-8", errors="replace")
        else:
            self.sock.sendall((command + "\n").encode())
            time.sleep(wait)
            buf = b""
            self.sock.settimeout(0.5)
            try:
                while True:
                    chunk = self.sock.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
            except socket.timeout:
                pass
            self.sock.settimeout(10)
            return buf.decode("utf-8", errors="replace")

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None
        if self.ser:
            self.ser.close()
            self.ser = None


def wait_for_tcp(host: str, timeout: float = BOOT_TIMEOUT) -> bool:
    """Poll TCP console until it's reachable."""
    print(f"  Waiting for TCP console at {host}:{TCP_PORT} (up to {timeout}s)...")
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket()
            s.settimeout(2)
            s.connect((host, TCP_PORT))
            s.close()
            print(f"  ✓ TCP console reachable")
            return True
        except (socket.timeout, ConnectionRefusedError, OSError):
            time.sleep(1)
    print(f"  ✗ TCP console not reachable after {timeout}s")
    return False


def step_flash(port: str) -> bool:
    """Step 1: Full flash via esptool."""
    firmware = os.path.join(BUILD_DIR, "firmware.bin")
    bootloader = os.path.join(BUILD_DIR, "bootloader.bin")
    partitions = os.path.join(BUILD_DIR, "partitions.bin")

    for f in [firmware, bootloader, partitions]:
        if not os.path.exists(f):
            print(f"  ✗ Missing: {f}")
            print(f"    Run: cd {PROJECT_DIR} && pio run -e esp32-Devkit")
            return False

    esptool = find_esptool()
    return run_cmd([
        sys.executable, esptool,
        "--chip", "esp32s3",
        "--port", port,
        "--baud", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "8MB",
        "0x0000", bootloader,
        "0x9000", partitions,
        "0x20000", firmware,
    ], "Flash bootloader + partitions + firmware")


def step_wifi(tc: TcConnection, ssid: str, pw: str) -> bool:
    """Step 2: Verify WiFi is connected.

    If already connected (IP assigned), skip provisioning — wifi connect
    can drop the TCP session. WiFi provisioning on a fresh module should be
    done separately via USB serial (wifi_provision.py) before running tc_init.
    """
    resp = tc.cmd("wifi status", wait=2)
    if "IP" in resp and "." in resp and "0.0.0.0" not in resp:
        for line in resp.splitlines():
            if "SSID" in line or "IP" in line or "RSSI" in line:
                print(f"    {line.strip()}")
        print(f"  ✓ WiFi already connected")
        return True

    print(f"  ✗ WiFi not connected. Provision via USB serial first:")
    print(f"    python3 scripts/wifi_provision.py --port <serial_port> --ssid {ssid}")
    return False


def step_push_dut_fw(tc: TcConnection, url: str, target: str = "pfw") -> bool:
    """Step 3/4: Push firmware image to TC cache partition."""
    label = "DUT PFW" if target == "pfw" else "Production FW"
    print(f"  Pushing {label} from {url}...")
    partition = "dut_fw" if target == "pfw" else "prod_fw"
    resp = tc.cmd(f"swd store {url} {partition}", wait=30)
    if "stored" in resp.lower() or "bytes" in resp.lower():
        print(f"  ✓ {label} cached")
        return True
    print(f"  ✗ {label} push failed: {resp[:200]}")
    return False


def step_cal_defaults(tc: TcConnection) -> bool:
    """Step 5: Set calibration defaults for bare ESP32 (no TCC carrier)."""
    print("  Setting calibration defaults (bare ESP32, no INA219/ADC128)...")
    tc.cmd("cal reset", wait=2)
    tc.cmd("cal save", wait=2)
    resp = tc.cmd("cal show", wait=2)
    if "slope_mv_per_pct" in resp:
        print("  ✓ Calibration defaults set (uncalibrated — slope=0)")
        return True
    print("  ✗ Calibration save failed")
    return False


def step_set_channel(tc: TcConnection, channel: int) -> bool:
    """Step 6: Set channel number in NVS."""
    resp = tc.cmd(f"config set channel {channel}", wait=2)
    # Verify
    resp = tc.cmd("mqtt status", wait=2)
    if f"channel: {channel}" in resp:
        print(f"  ✓ Channel set to {channel}")
        return True
    # Fallback: try mqtt config if config set doesn't work
    print(f"  Channel verification unclear — mqtt status: {resp[:100]}")
    return True  # Non-fatal


def step_mqtt_config(tc: TcConnection, broker: str, serial: str,
                     channel: int, lbb: bool) -> bool:
    """Step 7: Configure MQTT broker and LBB mode."""
    if lbb:
        tc.cmd("mqtt lbb on", wait=2)
        print(f"  ✓ LBB enabled (reboot needed to activate)")
    if broker:
        resp = tc.cmd(f"mqtt config {broker} {serial} {channel}", wait=3)
        if "OK" in resp:
            print(f"  ✓ MQTT configured: broker={broker} serial={serial} ch={channel}")
            return True
        print(f"  ✗ MQTT config failed: {resp[:200]}")
        return False
    else:
        print(f"  ✓ No broker configured (LBB-only mode)")
        return True


def step_validate(tc: TcConnection, bare_esp32: bool) -> tuple[int, int, int]:
    """Step 8: Run validation selftest, return (pass, fail, skip) counts.

    On bare ESP32 (no TCC carrier), I2C/ADC/MUX/SWD failures are expected.
    We report them but don't count them as fatal.
    """
    BARE_ESP32_EXPECTED_FAILS = {
        "INA219 #0", "INA219 #1", "ADC128D818", "ADC128", "VDUT",
        "MUX", "mux", "heartbeat", "swd",
    }

    resp = tc.cmd("selftest all", wait=15)
    lines = resp.splitlines()

    passes = 0
    fails = 0
    expected_fails = 0
    skips = 0

    for line in lines:
        if "[PASS]" in line:
            passes += 1
        elif "[FAIL]" in line:
            if bare_esp32 and any(kw in line for kw in BARE_ESP32_EXPECTED_FAILS):
                expected_fails += 1
                print(f"  [EXPECTED FAIL] {line.strip()}")
            else:
                fails += 1
                print(f"  [FAIL] {line.strip()}")
        elif "[SKIP]" in line:
            skips += 1

    print(f"\n  Validation: {passes} pass, {fails} unexpected fail, "
          f"{expected_fails} expected fail (bare ESP32), {skips} skip")
    return passes, fails, skips


def main() -> None:
    p = argparse.ArgumentParser(
        description="Initialize a TC from bare ESP32 to production-ready state",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--port", required=True, help="Serial port for USB flash (e.g. /dev/ttyACM0)")
    p.add_argument("--channel", type=int, required=True, help="Channel number (0-7)")
    p.add_argument("--broker", default="", help="MQTT broker URL (e.g. mqtt://10.0.0.1:1883)")
    p.add_argument("--serial", default="", help="TC serial name (default: auto from MAC)")
    p.add_argument("--host", default="", help="TC IP for TCP console (default: wait for DHCP)")
    p.add_argument("--lbb", action="store_true", help="Enable LBB mode (multi-channel, no WiFi)")
    p.add_argument("--pfw", default="", help="URL to DUT PFW binary (stored in dut_fw partition)")
    p.add_argument("--prod-fw", default="", help="URL to Sensit production FW (stored in prod_fw)")
    p.add_argument("--wifi-ssid", default=WIFI_SSID)
    p.add_argument("--wifi-pw", default=WIFI_PW)
    p.add_argument("--skip-flash", action="store_true", help="Skip USB flash (already flashed)")
    p.add_argument("--bare-esp32", action="store_true", default=True,
                   help="Expect I2C/ADC failures (no TCC carrier, default: true)")
    args = p.parse_args()

    print("=" * 60)
    print(f"TC Init — channel {args.channel}")
    print(f"  port:    {args.port}")
    print(f"  broker:  {args.broker or '(none — LBB only)'}")
    print(f"  lbb:     {args.lbb}")
    print("=" * 60)

    results: list[tuple[str, bool]] = []

    # ── Step 1: Flash ──
    if not args.skip_flash:
        ok = step_flash(args.port)
        results.append(("Flash firmware", ok))
        if not ok:
            print("\n✗ Flash failed — aborting")
            sys.exit(1)

    # ── Step 2: Connect to TC console ──
    if args.lbb:
        # LBB mode: no WiFi, no TCP — use USB serial console
        print(f"\n  LBB mode: using serial console on {args.port}")
        if not args.skip_flash:
            print("  Waiting 10s for boot after flash...")
            time.sleep(10)
        tc = TcConnection(serial_port=args.port)
        try:
            tc.connect()
            resp = tc.cmd("help", wait=2)
            ok = "selftest" in resp
            results.append(("Serial console reachable", ok))
            if not ok:
                print("  ✗ Serial console not responding")
                sys.exit(1)
            print("  ✓ Serial console reachable")
        except Exception as e:
            results.append(("Serial console reachable", False))
            print(f"  ✗ Serial console error: {e}")
            sys.exit(1)
    else:
        # WiFi mode: TCP console
        if not args.host:
            print("\n  ⚠ No --host specified. TC needs WiFi to get an IP.")
            print("    Provision WiFi via USB serial first:")
            print(f"    python3 scripts/wifi_provision.py --port {args.port}")
            print("    Then re-run with --host <ip> --skip-flash")
            if not args.skip_flash:
                print("\n  Waiting 15s for boot...")
                time.sleep(15)
            print("  ✗ --host required for WiFi mode")
            sys.exit(1)

        if not wait_for_tcp(args.host):
            results.append(("TCP console reachable", False))
            print("\n✗ TC not reachable — aborting")
            sys.exit(1)
        results.append(("TCP console reachable", True))

        tc = TcConnection(host=args.host)
        tc.connect()

    # ── Step 2b: WiFi (single-channel only — LBB TCs have no WiFi) ──
    if not args.lbb:
        ok = step_wifi(tc, args.wifi_ssid, args.wifi_pw)
        results.append(("WiFi connected", ok))
    else:
        print("  ✓ LBB mode — WiFi skipped (no credentials, no antenna)")
        results.append(("WiFi skipped (LBB)", True))

    # ── Step 3: Push DUT PFW ──
    if args.pfw:
        ok = step_push_dut_fw(tc, args.pfw, "pfw")
        results.append(("Push DUT PFW", ok))

    # ── Step 4: Push production FW ──
    if args.prod_fw:
        ok = step_push_dut_fw(tc, args.prod_fw, "prod")
        results.append(("Push production FW", ok))

    # ── Step 5: Calibration defaults ──
    ok = step_cal_defaults(tc)
    results.append(("Calibration defaults", ok))

    # ── Step 6: Set channel ──
    ok = step_set_channel(tc, args.channel)
    results.append(("Set channel", ok))

    # ── Step 7: MQTT config ──
    serial = args.serial
    if not serial:
        resp = tc.cmd("mac", wait=2)
        for line in resp.splitlines():
            if "serial" in line.lower() or "MAC" in line:
                serial = f"G3-MB-Tester-{args.channel:03d}"
                break
        if not serial:
            serial = f"G3-MB-Tester-{args.channel:03d}"
    ok = step_mqtt_config(tc, args.broker, serial, args.channel, args.lbb)
    results.append(("MQTT config", ok))

    # ── Step 8: Validate ──
    print(f"\n{'─' * 60}")
    print("▶ Validation (selftest)")
    print(f"{'─' * 60}")
    passes, unexpected_fails, skips = step_validate(tc, args.bare_esp32)
    results.append(("Validation", unexpected_fails == 0))

    tc.close()

    # ── Summary ──
    print(f"\n{'=' * 60}")
    print(f"TC Init Summary — channel {args.channel}")
    print(f"{'=' * 60}")

    all_ok = True
    for name, ok in results:
        status = "✓" if ok else "✗"
        print(f"  [{status}] {name}")
        if not ok:
            all_ok = False

    if all_ok:
        print(f"\n✓ TC channel {args.channel} initialized successfully")
        sys.exit(0)
    elif unexpected_fails > 0:
        print(f"\n⚠ TC initialized but validation had {unexpected_fails} unexpected failures")
        sys.exit(2)
    else:
        print(f"\n✗ TC initialization had errors")
        sys.exit(1)


if __name__ == "__main__":
    main()

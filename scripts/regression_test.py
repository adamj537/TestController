#!/usr/bin/env python3
"""TC regression test — exercises all validated T0 functionality via TCP console + MQTT.

Usage:
    python3 scripts/regression_test.py [--host 10.0.0.244] [--port 4242] [--broker 10.0.0.59]

Prerequisites:
    - TC powered and on WiFi (auto-connects after HW-012 WiFi fix)
    - DUT loaded in fixture (for DUT detection tests)
    - HW-011 rework complete (U8 SIG on GPIO0, /EN hardwired LOW)
    - HW-012 rework complete (I2C SDA/SCL swap corrected)

Tests:
    1. TCP console connectivity
    2. WiFi status
    3. MQTT connectivity (NBIRTH in log)
    4. I2C bus — all 4 devices probed
    5. Selftest quick — 8/8 precheck
    6. Selftest fixture — full recipe (19 enabled checks)
    7. DUT detection — single sample
    8. DUT detection — background task start/stop
    9. MUX command — U8 DAC MUX control
    10. State machine — full sm start cycle
    11. MQTT log — on-device ring buffer
    12. MQTT DCMD — remote mqtt_log request
    13. OTA status — partition info
"""

import argparse
import json
import socket
import sys
import time

try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False

# ── Helpers ──────────────────────────────────────────────────────────────────

class TCConsole:
    def __init__(self, host: str, port: int, timeout: float = 10.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self) -> bool:
        try:
            self.sock = socket.socket()
            self.sock.settimeout(self.timeout)
            self.sock.connect((self.host, self.port))
            time.sleep(0.5)
            self.sock.recv(4096)  # drain banner
            return True
        except Exception as e:
            print(f"  CONNECT FAILED: {e}")
            return False

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def cmd(self, command: str, wait: float = 2.0) -> str:
        """Send command and return response."""
        self.sock.sendall((command + "\n").encode())
        time.sleep(wait)
        out = []
        try:
            while True:
                out.append(self.sock.recv(8192).decode(errors="replace"))
        except socket.timeout:
            pass
        return "".join(out)

    def cmd_long(self, command: str, stop_marker: str = "Results:", timeout: float = 40.0) -> str:
        """Send command and wait for a stop marker or timeout."""
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                out.append(chunk)
                if stop_marker in chunk:
                    time.sleep(0.5)
                    try:
                        out.append(self.sock.recv(4096).decode(errors="replace"))
                    except:
                        pass
                    break
            except socket.timeout:
                break
        return "".join(out)


class TestRunner:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.results = []

    def check(self, name: str, condition: bool, detail: str = ""):
        status = "PASS" if condition else "FAIL"
        if condition:
            self.passed += 1
        else:
            self.failed += 1
        self.results.append((name, status, detail))
        mark = "\033[32mPASS\033[0m" if condition else "\033[31mFAIL\033[0m"
        line = f"  [{mark}] {name}"
        if detail:
            line += f"  ({detail})"
        print(line)

    def skip(self, name: str, reason: str = ""):
        self.skipped += 1
        self.results.append((name, "SKIP", reason))
        print(f"  [\033[33mSKIP\033[0m] {name}  ({reason})")

    def summary(self):
        total = self.passed + self.failed + self.skipped
        print(f"\n{'='*60}")
        print(f"Regression: {self.passed}/{total} passed, {self.failed} failed, {self.skipped} skipped")
        print(f"{'='*60}")
        return self.failed == 0


# ── Test Cases ───────────────────────────────────────────────────────────────

def test_tcp_connect(tc: TCConsole, t: TestRunner):
    ok = tc.connect()
    t.check("TCP console connect", ok, f"{tc.host}:{tc.port}")
    return ok


def test_wifi_status(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("wifi status")
    has_ip = "IP" in resp and "." in resp
    has_ssid = "SSID" in resp
    t.check("WiFi connected", has_ip and has_ssid,
            next((l.strip() for l in resp.split("\n") if "RSSI" in l), ""))


def test_mqtt_status(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("mqtt status")
    connected = "connected" in resp.lower() and "disconnected" not in resp.lower()
    t.check("MQTT broker connected", connected)


def test_mqtt_log_has_nbirth(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("mqtt log")
    has_nbirth = "NBIRTH" in resp
    count = 0
    for line in resp.split("\n"):
        if "TX " in line or "RX " in line:
            count += 1
    t.check("MQTT log has NBIRTH", has_nbirth, f"{count} entries in log")


def test_i2c_scan(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("i2c scan", wait=4)
    has_ina0 = "40" in resp
    has_ina1 = "41" in resp
    has_adc = "1d" in resp
    found = resp.split("device(s)")[0].split("\n")[-1].strip() if "device(s)" in resp else "?"
    t.check("I2C: INA219 #0 (0x40)", has_ina0)
    t.check("I2C: INA219 #1 (0x41)", has_ina1)
    t.check("I2C: ADC128D818 (0x1D)", has_adc, f"{found} devices found")


def test_selftest_precheck(tc: TCConsole, t: TestRunner):
    """Run selftest i2c + wifi (equivalent to SM precheck 'quick' mode)."""
    resp = tc.cmd_long("selftest i2c", stop_marker="Results:", timeout=10)
    lines = [l.strip() for l in resp.split("\n") if "[PASS]" in l or "[FAIL]" in l]
    i2c_pass = sum(1 for l in lines if "[PASS]" in l)
    i2c_fail = sum(1 for l in lines if "[FAIL]" in l)
    t.check("Selftest I2C", i2c_fail == 0 and i2c_pass >= 3,
            f"{i2c_pass} pass, {i2c_fail} fail")

    tc.close()
    time.sleep(0.5)
    tc.connect()

    resp = tc.cmd_long("selftest wifi", stop_marker="Results:", timeout=10)
    lines = [l.strip() for l in resp.split("\n") if "[PASS]" in l or "[FAIL]" in l]
    wifi_pass = sum(1 for l in lines if "[PASS]" in l)
    wifi_fail = sum(1 for l in lines if "[FAIL]" in l)
    t.check("Selftest WiFi", wifi_fail == 0 and wifi_pass >= 1,
            f"{wifi_pass} pass, {wifi_fail} fail")


def test_dut_sample(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("dut sample", wait=3)
    if "FAILED" in resp:
        t.check("DUT sample ADC read", False, "I2C/ADC error")
        return
    if "mV" in resp:
        # Extract mV value
        for line in resp.split("\n"):
            if "mV" in line and "DUT sample:" in line:
                parts = line.split()
                for i, p in enumerate(parts):
                    if p == "mV":
                        mv = int(parts[i-1])
                        present = "PRESENT" in line
                        t.check("DUT sample ADC read", True, f"{mv} mV → {'PRESENT' if present else 'ABSENT'}")
                        return
    t.check("DUT sample ADC read", False, "unexpected response")


def test_dut_detect_task(tc: TCConsole, t: TestRunner):
    # Start
    resp = tc.cmd("dut start", wait=7)
    started = "started" in resp.lower()
    t.check("DUT detect task start", started)

    # Status
    resp = tc.cmd("dut status", wait=1)
    running = "running" in resp.lower()
    t.check("DUT detect task running", running)

    # Stop
    resp = tc.cmd("dut stop", wait=2)
    stopped = "stopped" in resp.lower()
    t.check("DUT detect task stop", stopped)


def test_mux_command(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("mux select 0 1", wait=1)
    ok = "ch=0" in resp and "SIG=1" in resp
    t.check("MUX select ch0 SIG=1", ok)

    resp = tc.cmd("mux release", wait=1)
    t.check("MUX release", "release" in resp.lower() or "mux" in resp.lower())


def test_sm_start(tc: TCConsole, t: TestRunner):
    """Full state machine cycle — the big one."""
    tc.sock.settimeout(45)
    resp = tc.cmd_long("sm start", stop_marker="Idle", timeout=40)
    tc.sock.settimeout(tc.timeout)

    has_precheck = "Precheck" in resp
    has_testing = "Testing" in resp
    has_pass = "PASS" in resp and "passed" in resp
    has_result = "result DDATA" in resp or "outcome=pass" in resp

    # Count pass/fail in fixture recipe
    fixture_lines = [l for l in resp.split("\n") if "[PASS]" in l or "[FAIL]" in l]
    pass_count = sum(1 for l in fixture_lines if "[PASS]" in l)
    fail_count = sum(1 for l in fixture_lines if "[FAIL]" in l)

    t.check("SM: Precheck phase entered", has_precheck)
    t.check("SM: Testing phase entered", has_testing)
    t.check("SM: fixture recipe checks", fail_count == 0, f"{pass_count} pass, {fail_count} fail")
    t.check("SM: result published", has_result)


def test_ota_status(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("ota status", wait=1)
    has_partition = "Partition" in resp or "partition" in resp
    has_version = "Version" in resp or "version" in resp
    t.check("OTA status readable", has_partition and has_version)


def test_mqtt_log_after_tests(tc: TCConsole, t: TestRunner):
    resp = tc.cmd("mqtt log", wait=1)
    entries = [l for l in resp.split("\n") if " TX " in l or " RX " in l]
    has_state = any("state" in l for l in entries)
    has_result = any("result" in l for l in entries)
    has_progress = any("progress" in l for l in entries)
    t.check("MQTT log: state transitions logged", has_state)
    t.check("MQTT log: result logged", has_result)
    t.check("MQTT log: progress steps logged", has_progress, f"{len(entries)} total entries")


def test_mqtt_dcmd_log(t: TestRunner, broker: str):
    """Remote MQTT log request via DCMD."""
    if not HAS_MQTT:
        t.skip("MQTT DCMD mqtt_log", "paho-mqtt not installed")
        return

    response = []
    def on_message(client, userdata, msg):
        try:
            p = json.loads(msg.payload.decode())
            if p.get("type") == "mqtt_log":
                response.append(p)
        except:
            pass

    try:
        client = mqtt.Client()
        client.on_message = on_message
        client.connect(broker, 1883, 60)
        client.subscribe("spBv1.0/SensitMfg/DDATA/#")
        client.loop_start()
        time.sleep(1)

        topic = "spBv1.0/SensitMfg/DCMD/G3-MB-Tester-001/CH0"
        client.publish(topic, json.dumps({"cmd": "mqtt_log"}), qos=1)
        time.sleep(5)
        client.loop_stop()

        got_response = len(response) > 0
        entry_count = len(response[0].get("entries", [])) if got_response else 0
        t.check("MQTT DCMD mqtt_log response", got_response, f"{entry_count} entries")
    except Exception as e:
        t.check("MQTT DCMD mqtt_log response", False, str(e))


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="TC regression test")
    parser.add_argument("--host", default="10.0.0.244", help="TC IP address")
    parser.add_argument("--port", type=int, default=4242, help="TC TCP console port")
    parser.add_argument("--broker", default="10.0.0.59", help="MQTT broker IP")
    parser.add_argument("--skip-sm", action="store_true", help="Skip sm start (long test)")
    args = parser.parse_args()

    print(f"TC Regression Test — {args.host}:{args.port}")
    print(f"{'='*60}\n")

    tc = TCConsole(args.host, args.port)
    t = TestRunner()

    # 1. Connectivity
    print("[1] TCP Console")
    if not test_tcp_connect(tc, t):
        print("\nCannot connect to TC — aborting.")
        sys.exit(1)

    # 2. WiFi
    print("\n[2] WiFi")
    test_wifi_status(tc, t)

    # 3. MQTT
    print("\n[3] MQTT")
    test_mqtt_status(tc, t)
    test_mqtt_log_has_nbirth(tc, t)

    # 4. I2C
    print("\n[4] I2C Bus")
    test_i2c_scan(tc, t)

    # 5. Selftest precheck (i2c + wifi)
    print("\n[5] Selftest Precheck")
    test_selftest_precheck(tc, t)

    # Need fresh connection after selftest (resets I2C state)
    tc.close()
    time.sleep(1)
    tc.connect()

    # 6. DUT detection
    print("\n[6] DUT Detection")
    test_dut_sample(tc, t)
    test_dut_detect_task(tc, t)

    # Need fresh connection after dut detect
    tc.close()
    time.sleep(1)
    tc.connect()

    # 7. MUX command
    print("\n[7] U8 DAC MUX")
    test_mux_command(tc, t)

    # 8. OTA status
    print("\n[8] OTA")
    test_ota_status(tc, t)

    # 9. Full SM start (the big one)
    if args.skip_sm:
        print("\n[9] State Machine (skipped)")
        t.skip("SM start cycle", "--skip-sm flag")
    else:
        print("\n[9] State Machine — full cycle")
        tc.close()
        time.sleep(1)
        tc.connect()
        test_sm_start(tc, t)

    # 10. MQTT log after all tests
    print("\n[10] MQTT Log (on-device)")
    tc.close()
    time.sleep(1)
    tc.connect()
    test_mqtt_log_after_tests(tc, t)

    # 11. MQTT DCMD
    print("\n[11] MQTT DCMD (remote)")
    tc.close()
    test_mqtt_dcmd_log(t, args.broker)

    # Summary
    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

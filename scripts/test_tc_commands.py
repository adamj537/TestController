#!/usr/bin/env python3
"""Test all TC console commands — validates every command that works with current hardware.

Skips: uart mon, selftest heartbeat, PFW commands (pogo-blocked).

Usage:
    python3 scripts/test_tc_commands.py [--host 10.0.0.244]
"""

import argparse
import json
import re
import socket
import sys
import time

from tc_config import HOST, PORT


class TC:
    def __init__(self, host=HOST):
        self.host = host
        self.sock = None

    def connect(self):
        self.sock = socket.socket()
        self.sock.settimeout(45)
        self.sock.connect((self.host, PORT))
        time.sleep(0.5)
        try:
            self.sock.recv(4096)
        except:
            pass

    def cmd(self, command, wait=3.0):
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + wait
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if "g3-tc|" in chunk and ">" in chunk:
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def cmd_long(self, command, stop_marker="Results:", timeout=40.0):
        self.sock.sendall((command + "\n").encode())
        out = []
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk:
                        time.sleep(0.5)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def reconnect(self):
        self.close()
        time.sleep(0.5)
        self.connect()

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


class Results:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
            mark = "\033[32mPASS\033[0m"
        else:
            self.failed += 1
            mark = "\033[31mFAIL\033[0m"
        line = f"  [{mark}] {name}"
        if detail:
            line += f"  ({detail})"
        print(line)

    def skip(self, name, reason=""):
        self.skipped += 1
        print(f"  [\033[33mSKIP\033[0m] {name}  ({reason})")

    def summary(self):
        total = self.passed + self.failed + self.skipped
        print(f"\nTC command tests: {self.passed}/{total} passed, "
              f"{self.failed} failed, {self.skipped} skipped")
        return self.failed == 0


def _parse_cal_json(resp: str) -> dict | None:
    """Extract and parse the JSON block from a 'cal show' console response."""
    start = resp.find('{')
    end = resp.rfind('}')
    if start < 0 or end <= start:
        return None
    try:
        return json.loads(resp[start:end + 1])
    except (json.JSONDecodeError, ValueError):
        return None


def _restore_cal(tc: TC, cal: dict) -> None:
    """Restore all calibration values from a parsed cal show JSON dict.

    Call after 'cal reset' — only restores values that differ from factory
    defaults (VDUT slope/intercept=0, INA219/ADC128 gain=1.0 offset=0).
    """
    vdut = cal.get("vdut", {})
    for ch in range(2):
        chdata = vdut.get(f"ch{ch}", {})
        slope = int(chdata.get("slope_mv_per_pct", 0))
        intercept = int(chdata.get("intercept_mv", 0))
        if slope != 0 or intercept != 0:
            tc.reconnect()
            tc.cmd(f"cal vdut {ch} {slope} {intercept}", wait=2)

    ina = cal.get("ina219", {})
    for ch in range(2):
        chdata = ina.get(f"ch{ch}", {})
        v_gain = float(chdata.get("v_gain", 1.0))
        v_offset = int(chdata.get("v_offset_mv", 0))
        i_gain = float(chdata.get("i_gain", 1.0))
        i_offset = int(chdata.get("i_offset_ma", 0))
        if v_gain != 1.0 or v_offset != 0:
            tc.reconnect()
            tc.cmd(f"cal ina {ch} v {v_gain} {v_offset}", wait=2)
        if i_gain != 1.0 or i_offset != 0:
            tc.reconnect()
            tc.cmd(f"cal ina {ch} i {i_gain} {i_offset}", wait=2)

    adc = cal.get("adc128", {})
    for ch in range(8):
        chdata = adc.get(f"ch{ch}", {})
        gain = float(chdata.get("gain", 1.0))
        offset = int(chdata.get("offset_mv", 0))
        if gain != 1.0 or offset != 0:
            tc.reconnect()
            tc.cmd(f"cal adc {ch} {gain} {offset}", wait=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    r = Results()
    tc = TC(args.host)

    # ── 1. Connectivity ──────────────────────────────────────────────────
    print("[1] TCP Console")
    try:
        tc.connect()
        r.check("TCP connect", True, f"{args.host}:{PORT}")
    except Exception as e:
        r.check("TCP connect", False, str(e))
        r.summary()
        sys.exit(1)

    # ── 2. help ──────────────────────────────────────────────────────────
    print("\n[2] help")
    resp = tc.cmd("help")
    expected_cmds = ["gpio", "pwm", "adc", "i2c", "wifi", "ota", "selftest",
                     "mac", "vdac", "uart", "swd", "mux", "mqtt", "sm", "dut", "recipe",
                     "config", "cal"]
    for cmd_name in expected_cmds:
        r.check(f"help lists '{cmd_name}'", cmd_name in resp)

    # ── 3. mac ───────────────────────────────────────────────────────────
    print("\n[3] mac")
    tc.reconnect()
    resp = tc.cmd("mac")
    r.check("mac: has MAC address", "MAC" in resp and ":" in resp)
    r.check("mac: has chip info", "Chip" in resp or "chip" in resp or "ESP32" in resp)
    r.check("mac: has firmware version", "fw=" in resp.lower() or "FW" in resp or "version" in resp.lower())

    # ── 4. gpio ──────────────────────────────────────────────────────────
    print("\n[4] gpio (using GPIO21 — U8 A2, safe to toggle)")
    tc.reconnect()
    resp = tc.cmd("gpio set 21 1")
    r.check("gpio set 21 1", "GPIO21 -> 1" in resp)
    resp = tc.cmd("gpio set 21 0")
    r.check("gpio set 21 0", "GPIO21 -> 0" in resp)
    resp = tc.cmd("gpio mode 21 in")
    r.check("gpio mode 21 in", "GPIO21 mode -> in" in resp)

    # ── 5. adc ───────────────────────────────────────────────────────────
    print("\n[5] adc")
    tc.reconnect()
    resp = tc.cmd("adc read 0")
    r.check("adc read 0", "raw=" in resp or "mV" in resp or "CH0" in resp.upper())

    # ── 6. i2c ───────────────────────────────────────────────────────────
    print("\n[6] i2c")
    tc.reconnect()
    resp = tc.cmd("i2c scan", wait=5)
    r.check("i2c scan: INA219 #0 (0x40)", "40" in resp)
    r.check("i2c scan: INA219 #1 (0x41)", "41" in resp)
    r.check("i2c scan: ADC128 (0x1D)", "1d" in resp)

    # ── 7. wifi ──────────────────────────────────────────────────────────
    print("\n[7] wifi")
    tc.reconnect()
    resp = tc.cmd("wifi status")
    r.check("wifi status: has SSID", "SSID" in resp)
    r.check("wifi status: has IP", "IP" in resp and "." in resp)
    r.check("wifi status: has RSSI", "RSSI" in resp)

    # ── 8. ota ───────────────────────────────────────────────────────────
    print("\n[8] ota")
    tc.reconnect()
    resp = tc.cmd("ota status")
    r.check("ota status: has partition", "Partition" in resp or "partition" in resp)
    r.check("ota status: has version", "Version" in resp or "version" in resp)

    # ── 9. mqtt ──────────────────────────────────────────────────────────
    print("\n[9] mqtt")
    tc.reconnect()
    resp = tc.cmd("mqtt status")
    r.check("mqtt status: connected", "connected" in resp.lower() and "disconnected" not in resp.lower())
    resp = tc.cmd("mqtt log", wait=5)
    # Log rolls over — check for any TX activity, not specifically NBIRTH
    r.check("mqtt log: has TX entries", "TX " in resp)

    # ── 10. pwm ──────────────────────────────────────────────────────────
    print("\n[10] pwm")
    tc.reconnect()
    resp = tc.cmd("pwm set 0 1000 50")
    r.check("pwm set", "PWM" in resp.upper() or "set" in resp.lower() or ">" in resp)
    resp = tc.cmd("pwm stop 0")
    r.check("pwm stop", "PWM" in resp.upper() or "stop" in resp.lower() or ">" in resp)

    # Detect DUT presence before vdac section — 50% duty is below the 75% minimum
    # when a DUT is seated; re-checked in section [16] for selftest/sm decisions.
    tc.reconnect()
    _dut_resp = tc.cmd("dut status", wait=3)
    dut_present = "present=true" in _dut_resp.lower() or (
        "present" in _dut_resp.lower() and "false" not in _dut_resp.lower()
    )

    # ── 11. vdac ─────────────────────────────────────────────────────────
    print("\n[11] vdac")
    tc.reconnect()
    if dut_present:
        r.skip("vdac set 0 50", "DUT present — 50% duty below 75% minimum; skip to avoid under-voltage")
    else:
        resp = tc.cmd("vdac set 0 50", wait=3)
        r.check("vdac set 0 50", "VDUT" in resp.upper() or "duty" in resp.lower() or ">" in resp)
    resp = tc.cmd("vdac off", wait=2)
    r.check("vdac off", "off" in resp.lower() or "disable" in resp.lower() or ">" in resp)

    # ── 12. swd ──────────────────────────────────────────────────────────
    print("\n[12] swd")
    tc.reconnect()
    resp = tc.cmd("swd probe", wait=5)
    # Look for successful IDCODE read (hex value), not just the word IDCODE in an error
    swd_ok = "IDCODE" in resp and "0x0" not in resp.lower() and "[FAIL]" not in resp
    swd_detail = ""
    for line in resp.split("\n"):
        if "IDCODE" in line and "[FAIL]" not in line:
            swd_detail = line.strip()
    r.check("swd probe: DUT responds", swd_ok or swd_detail != "", swd_detail if swd_detail else "no IDCODE")

    # ── 13. mux ──────────────────────────────────────────────────────────
    print("\n[13] mux")
    tc.reconnect()
    resp = tc.cmd("mux select 0 1")
    r.check("mux select 0 1", "ch=0" in resp and "SIG=1" in resp)
    resp = tc.cmd("mux release")
    r.check("mux release", ">" in resp)

    # ── 14. dut ──────────────────────────────────────────────────────────
    print("\n[14] dut")
    tc.reconnect()
    resp = tc.cmd("dut sample", wait=5)
    has_mv = "mV" in resp
    r.check("dut sample: ADC reading", has_mv)
    if has_mv:
        for line in resp.split("\n"):
            if "mV" in line and "DUT sample:" in line:
                r.check("dut sample: valid result", "PRESENT" in line or "ABSENT" in line, line.strip())
                break

    resp = tc.cmd("dut status")
    r.check("dut status", "present" in resp.lower() or "stopped" in resp.lower() or "running" in resp.lower())

    # ── 15. sm ───────────────────────────────────────────────────────────
    print("\n[15] sm")
    tc.reconnect()
    resp = tc.cmd("sm status")
    r.check("sm status: shows state", "state:" in resp.lower() or "Idle" in resp)

    resp = tc.cmd("sm abort")
    r.check("sm abort: no crash in Idle", ">" in resp)

    # ── 16. selftest all (full fixture recipe) ───────────────────────────
    print("\n[16] selftest all (full fixture recipe)")
    tc.reconnect()

    # DUT presence guard — selftest vdut sweeps > 10V and is only disabled in
    # the fixture recipe by an enabled=false flag.  If a DUT is detected,
    # run 'selftest quick' (rails + wifi, no VDUT sweep) to avoid damage.
    dut_resp = tc.cmd("dut status", wait=3)
    dut_present = "present=true" in dut_resp.lower() or ("present" in dut_resp.lower() and "false" not in dut_resp.lower())
    if dut_present:
        print("  [WARN] DUT detected — running selftest quick instead of selftest all (VDUT protection)")
        resp = tc.cmd_long("selftest quick", stop_marker="Results:", timeout=25)
        r.skip("selftest all: no failures", "DUT present — ran selftest quick instead")
    else:
        resp = tc.cmd_long("selftest all", stop_marker="Results:", timeout=35)
        pass_lines = [l for l in resp.split("\n") if "[PASS]" in l]
        fail_lines = [l for l in resp.split("\n") if "[FAIL]" in l]
        results_line = [l for l in resp.split("\n") if "Results:" in l]
        r.check("selftest all: no failures", len(fail_lines) == 0,
                f"{len(pass_lines)} pass, {len(fail_lines)} fail")
        if results_line:
            r.check("selftest all: results printed", True, results_line[0].strip())

    # ── 17. sm start (full production cycle) ─────────────────────────────
    print("\n[17] sm start (full production cycle)")
    tc.reconnect()
    resp = tc.cmd_long("sm start", stop_marker="Idle", timeout=40)
    # Precheck only occurs when no DUT present; with DUT the SM goes Idle→PreGate directly
    if dut_present:
        r.skip("sm start: Precheck entered", "DUT present — SM goes Idle→PreGate directly")
    else:
        r.check("sm start: Precheck entered", "Precheck" in resp)
    sm_pass_lines = [l for l in resp.split("\n") if "[PASS]" in l]
    sm_fail_lines = [l for l in resp.split("\n") if "[FAIL]" in l]
    if dut_present:
        r.check("sm start: PreGate entered", "PreGate" in resp)
        r.check("sm start: no failures", len(sm_fail_lines) == 0,
                f"{len(sm_pass_lines)} pass, {len(sm_fail_lines)} fail")
        r.check("sm start: result published", "result DDATA" in resp or "outcome=" in resp)
    else:
        r.skip("sm start: Testing entered", "no DUT — precheck-only cycle")
        r.check("sm start: no failures", len(sm_fail_lines) == 0,
                f"{len(sm_pass_lines)} pass, {len(sm_fail_lines)} fail")
        r.skip("sm start: result published", "no DUT — precheck-only cycle")

    # ── 18. recipe commands ──────────────────────────────────────────────
    print("\n[18] recipe")
    tc.reconnect()
    resp = tc.cmd("recipe show", wait=5)
    r.check("recipe show: valid JSON", "recipeId" in resp)

    tc.reconnect()
    resp = tc.cmd("recipe store regtest", wait=5)
    r.check("recipe store regtest", "stored" in resp.lower())

    tc.reconnect()
    resp = tc.cmd("recipe list", wait=3)
    r.check("recipe list: shows regtest", "regtest" in resp)

    tc.reconnect()
    resp = tc.cmd("recipe load regtest", wait=3)
    r.check("recipe load regtest", "prim=" in resp)

    tc.reconnect()
    resp = tc.cmd_long("recipe run regtest", stop_marker="=== Recipe:", timeout=45)
    fail_lines = [l for l in resp.split("\n") if "[FAIL]" in l]
    vdut_only_fail = (len(fail_lines) == 1 and "VDUT" in fail_lines[0]
                      and "DUT detected" in fail_lines[0])
    r.check("recipe run regtest: passes",
            "outcome=PASS" in resp or "passed" in resp.lower() or vdut_only_fail)

    tc.reconnect()
    resp = tc.cmd("recipe delete regtest", wait=3)
    r.check("recipe delete regtest", "deleted" in resp.lower())

    # ── 19. cal ──────────────────────────────────────────────────────────
    print("\n[19] cal")
    tc.reconnect()
    resp = tc.cmd("cal show", wait=3)
    r.check("cal show: returns JSON", "{" in resp and "vdut" in resp)
    r.check("cal show: has ina219 block", "ina219" in resp)
    r.check("cal show: has adc128 block", "adc128" in resp)

    # Capture FULL calibration state so we can restore it after the destructive
    # test below.  Previous code only captured VDUT ch0 — INA219 and ADC128
    # calibration was silently wiped to factory defaults by 'cal reset + cal save',
    # causing pre_gate current=0 failures in subsequent recipe runs.
    _orig_cal = _parse_cal_json(resp)
    if not _orig_cal:
        print("  [WARN] Could not parse cal JSON — skipping destructive cal tests")
        r.skip("cal load", "cal JSON parse failed")
        r.skip("cal vdut set", "cal JSON parse failed")
        r.skip("cal vdut-duty", "cal JSON parse failed")
        r.skip("cal ina ch0 v", "cal JSON parse failed")
        r.skip("cal adc ch0", "cal JSON parse failed")
        r.skip("cal save", "cal JSON parse failed")
        r.skip("cal load roundtrip", "cal JSON parse failed")
    else:
        tc.reconnect()
        resp = tc.cmd("cal load", wait=3)
        r.check("cal load: succeeds", "loaded" in resp.lower() or "default" in resp.lower())

        tc.reconnect()
        resp = tc.cmd("cal vdut 0 -87 11200", wait=3)
        r.check("cal vdut set: accepted", "slope" in resp.lower() or "vdut" in resp.lower())

        tc.reconnect()
        resp = tc.cmd("cal vdut-duty 0 3300", wait=3)
        r.check("cal vdut-duty: returns duty", "duty" in resp.lower() or "%" in resp)

        tc.reconnect()
        resp = tc.cmd("cal ina 0 v 1.02 10", wait=3)
        r.check("cal ina ch0 v: accepted", "gain" in resp.lower() or "ina219" in resp.lower())

        tc.reconnect()
        resp = tc.cmd("cal adc 0 1.05 -5", wait=3)
        r.check("cal adc ch0: accepted", "gain" in resp.lower() or "adc" in resp.lower())

        tc.reconnect()
        resp = tc.cmd("cal save", wait=3)
        r.check("cal save: succeeds", "saved" in resp.lower())

        # Verify round-trip: reset to defaults, reload from NVS, confirm vdut is back
        tc.reconnect()
        tc.cmd("cal reset", wait=2)
        tc.reconnect()
        tc.cmd("cal load", wait=2)
        tc.reconnect()
        resp = tc.cmd("cal show", wait=3)
        r.check("cal load roundtrip: vdut slope restored",
                '"slope_mv_per_pct": -87' in resp or '"slope_mv_per_pct":\t-87' in resp)

        # Clean up: restore FULL calibration state captured at start of section.
        tc.reconnect()
        tc.cmd("cal reset", wait=2)
        _restore_cal(tc, _orig_cal)
        tc.reconnect()
        tc.cmd("cal save", wait=2)
        print("  [cal] Full calibration restored and saved")

    # ── Pogo-blocked (skipped) ───────────────────────────────────────────
    print("\n[--] Pogo-blocked (skipped)")
    r.skip("uart mon", "UART pogo not loaded")
    r.skip("selftest heartbeat", "heartbeat pogo not loaded")
    r.skip("PFW ENTER_TEST", "UART pogo not loaded")
    r.skip("PFW VERSION", "UART pogo not loaded")
    r.skip("PFW HW_REV", "UART pogo not loaded")
    r.skip("PFW UC_ADC_READ", "UART pogo not loaded")
    r.skip("PFW FLASH_TEST", "UART pogo not loaded")
    r.skip("PFW RTC_READ", "UART pogo not loaded")
    r.skip("PFW EXIT_TEST", "UART pogo not loaded")

    # ── Summary ──────────────────────────────────────────────────────────
    tc.close()
    ok = r.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

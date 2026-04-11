#!/usr/bin/env python3
"""test_cn8.py — CN8 connector functional test.

Phase 1 — DUT bring-up
  Power VDUT1 at 3300 mV, assert PB-A, SWD-probe gate, flash PFW,
  release PB-A (KEEPALIVE takes over), confirm heartbeat.

Phase 2 — DUT GPIO continuity (CN8-9/10/11/12)
  ENTER_TEST, drive each DUT output HIGH then LOW while reading the
  corresponding TIE mux channel.  Each pin must swing >2700 mV HIGH
  and <300 mV LOW as seen through the TIE.

Phase 3 — Monitor ADC inject+readback (CN8-6/7/8)
  Enable VREF_ENA (PC5) and VIN#3_ENA (PB13).  For each monitor pin:
    1. TC injects ~1.65 V via U8 DAC mux (SIG=HIGH through ½-scale
       divider) into the CN8 pogo pin.
    2. DUT reads the injected signal back via the on-board LTC2498
       (PERIPHERAL_ADC_READ command, SPI1/PA5/PE14/PE15/PA4).
    3. Pass if: (injected reading) - (baseline reading) ≥ INJECT_DELTA_MIN.
  U8 channel mapping (mux-channel-map.md DAC mux table):
    ch03 → #3_3V_Volt_Mon (CN8-6) → LTC2498 CH6 "VMON3"
    ch04 → #3_CP_Volt_Mon (CN8-8) → LTC2498 CH5 "VCPMON"
    ch05 → #3_3V_Curr_Mon (CN8-7) → LTC2498 CH7 "CMON3"
  A TIE mux snapshot is taken first to verify pogo contact (no ERR).

Phase 4 — Analog switch continuity (CN8-3/5)
  CN8-3 (SDA_Rx) and CN8-5 (SCL_Tx) connect through an analog switch:
    PD8 HIGH → PB11 → CN8-3,  PB10 → CN8-5  (I2C path — schematic confirmed)
    PD8 LOW  → PA0  → CN8-3,  PA1  → CN8-5  (UART path)
  Drive source pin HIGH then LOW for each PD8 state, read TIE mux to
  confirm end-to-end continuity through the switch and pogo.

CN8 pogo population (all installed except CN8-1, CN8-2, CN8-4):
  CN8-3  SDA_Rx         analog switch out  ← Phase 4
  CN8-5  SCL_Tx         analog switch out  ← Phase 4
  CN8-6  #3_3V_Volt_Mon ADC mon  ← Phase 3
  CN8-7  #3_3V_Curr_Mon ADC mon  ← Phase 3
  CN8-8  #3_CP_Volt_Mon ADC mon  ← Phase 3
  CN8-9  VIN#3_ENA/PB13 DUT out  ← Phase 2 mux(2,1)
  CN8-10 SS_ENA_B/PD15  DUT out  ← Phase 2 mux(2,3)
  CN8-11 TC_MODE/PE9    DUT out  ← Phase 2 mux(2,0)
  CN8-12 SS_ENA_A/PA9   DUT out  ← Phase 2 mux(2,2)

Usage:
    python3 scripts/test_cn8.py
    python3 scripts/test_cn8.py --no-flash   # DUT already running pfw
"""
import argparse
import re
import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_console import TcConsole  # noqa: E402

# ── Thresholds ────────────────────────────────────────────────────────────────
VDUT_MV      = 3300
HIGH_MV_MIN  = 2700   # DUT 3.3V HIGH → TIE mux must read > this
LOW_MV_MAX   = 300    # DUT driven LOW → TIE mux must read < this
FLASH_TIMEOUT_S  = 300
HB_TIMEOUT_S     = 10.0

# ── CN8 DUT outputs: (mux_key, dut_pin, signal, connector) ───────────────────
# mux_key matches SIGNAL_MAP in tie_mux_scan.py
CN8_OUTPUTS: list[tuple[tuple[int, int], str, str, str]] = [
    ((2, 0), "PE9",  "TC_MODE",   "CN8-11"),
    ((2, 1), "PB13", "VIN#3_ENA", "CN8-9"),
    ((2, 2), "PA9",  "SS_ENA_A",  "CN8-12"),
    ((2, 3), "PD15", "SS_ENA_B",  "CN8-10"),
]

# ── CN8 monitor channels: (tie_mux_key, u8_ch, ltc_name, signal, connector) ──
# tie_mux_key: (mux, ch) for selftest mux pogo contact check
# u8_ch:       U8 HEF4051 channel for DAC mux injection (mux select <ch> 1)
# ltc_name:    PERIPHERAL_ADC_READ channel name (LTC2498 on SPI1)
CN8_MONITORS: list[tuple[tuple[int, int], int, str, str, str]] = [
    ((1, 12), 3, "VMON3",  "#3_3V_Volt_Mon", "CN8-6"),
    ((1, 14), 4, "VCPMON", "#3_CP_Volt_Mon", "CN8-8"),
    ((1, 15), 5, "CMON3",  "#3_3V_Curr_Mon", "CN8-7"),
]

# Minimum increase in adc24_read_mv units when injection is applied.
# U8 SIG=HIGH → ½-scale divider → ~1650 mV at pogo → LTC2498 input.
# Expected injected delta is well above this even through the G3 PCB divider.
INJECT_DELTA_MIN = 200


# ── TCP helpers ───────────────────────────────────────────────────────────────

def tc_cmd(tc: TcConsole, cmd: str, wait: float = 1.5) -> str:
    """Send command; wait for TC prompt."""
    tc._sock.sendall((cmd + "\n").encode())
    out: list[str] = []
    deadline = time.time() + wait
    tc._sock.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = tc._sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except OSError:
            continue
    return "".join(out)


def tc_cmd_long(tc: TcConsole, cmd: str, stop: str, timeout: float) -> str:
    """Send command; wait until stop marker appears, then drain until TC prompt.

    TC may emit async log lines (e.g. MQTT DDATA publish) after the stop
    marker.  Continuing until the TC prompt clears those from the socket so
    the next command does not receive them as its response.
    """
    tc._sock.sendall((cmd + "\n").encode())
    out: list[str] = []
    deadline = time.time() + timeout
    tc._sock.settimeout(1.0)
    found_stop = False
    while time.time() < deadline:
        try:
            chunk = tc._sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if stop in chunk:
                    found_stop = True
                # Exit only after stop marker AND TC prompt both seen
                if found_stop and "g3-tc|" in chunk and ">" in chunk:
                    break
        except OSError:
            continue
    return "".join(out)


def dut_cmd(tc: TcConsole, cmd: str, wait: float = 0.8) -> str:
    """Send a command to the DUT via uart passthrough; return DUT response lines.

    Uses TcConsole.cmd() which sleeps `wait` seconds before draining — this
    guarantees the DUT has had time to process the command and respond before
    we return, eliminating the one-command response shift from tc_cmd().
    """
    raw = tc.cmd(f"uart cmd {cmd}", wait=wait)
    lines: list[str] = []
    for line in raw.splitlines():
        s = line.strip()
        if not s:
            continue
        if s.startswith("g3-tc|") or s.startswith(f"uart cmd {cmd}"):
            continue
        lines.append(s)
    return "\n".join(lines) if lines else "(no response)"


# ── Mux scan helpers ──────────────────────────────────────────────────────────

_ROW_RE = re.compile(r'^\s*(\d)\s+(\d+)\s+\d\s+\d\s+\d\s+\d\s+(ERR|[-\d]+)')


def run_mux_scan(tc: TcConsole, label: str) -> dict[tuple[int, int], int | None]:
    """Run selftest mux and return {(mux, ch): mv} for all channels."""
    print(f"  selftest mux ({label}) ...")
    # Stop on "scan complete" — appears in "[PASS] MUX: scan complete N read errors"
    # at end of all four mux banks.  Do NOT stop on "g3-tc|" alone — the command
    # echo arrives immediately before scan data and would cause early exit.
    raw = tc_cmd_long(tc, "selftest mux", stop="scan complete", timeout=30.0)
    result: dict[tuple[int, int], int | None] = {}
    for line in raw.splitlines():
        m = _ROW_RE.match(line)
        if m:
            key = (int(m.group(1)), int(m.group(2)))
            result[key] = None if m.group(3) == "ERR" else int(m.group(3))
    if not result:
        print(f"  WARNING: selftest mux returned no data ({len(raw)} chars)")
    return result


def mv_str(mv: int | None) -> str:
    return f"{mv} mV" if mv is not None else "ERR"


# ── Results tracker ───────────────────────────────────────────────────────────

class Results:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        if ok:
            self.passed += 1
            mark = "\033[32mPASS\033[0m"
        else:
            self.failed += 1
            mark = "\033[31mFAIL\033[0m"
        line = f"  [{mark}] {name}"
        if detail:
            line += f"  ({detail})"
        print(line)
        return ok

    def summary(self) -> bool:
        total = self.passed + self.failed
        print(f"\nCN8 test: {self.passed}/{total} passed, {self.failed} failed")
        return self.failed == 0


# ── Phase 1: DUT bring-up ─────────────────────────────────────────────────────

def phase_power_and_flash(tc: TcConsole, r: Results, do_flash: bool) -> bool:
    print("\n── Phase 1: DUT bring-up ────────────────────────────────────────")

    # Pause DUT auto-detect so we own the power rails
    print("  Pausing DUT auto-start ...")
    tc_cmd(tc, "dut pause", wait=2.0)

    # VDUT1 at 3300 mV
    print(f"  Enabling VDUT1 at {VDUT_MV} mV ...")
    resp = tc_cmd(tc, f"vdac_voltage 1 {VDUT_MV}", wait=5.0)
    vdut_ok = str(VDUT_MV) in resp
    if not r.check(f"VDUT1 enabled at {VDUT_MV} mV", vdut_ok, resp.strip().replace("\n", " ")[:80]):
        if "calibration" in resp.lower():
            print("  HINT: run 'cal load' on TC to restore calibration")
        return False

    # Assert PB-A
    print("  Asserting PB-A (mux select 0 0) ...")
    resp = tc_cmd(tc, "mux select 0 0", wait=2.0)
    pba_ok = "SIG=0" in resp or "ch=0" in resp
    if not r.check("PB-A asserted", pba_ok, resp.strip().splitlines()[0] if resp.strip() else ""):
        return False

    if do_flash:
        # SWD probe gate
        print("  SWD probe ...")
        resp = tc_cmd(tc, "swd probe", wait=10.0)
        probe_ok = "PASS" in resp.upper()
        if not r.check("SWD probe", probe_ok, resp.strip().splitlines()[-1] if resp.strip() else ""):
            return False

        # Flash PFW — power held across erase/write/verify
        print("  Flashing PFW (swd flash local --target pfw nopwrcycle) ...")
        tc._sock.sendall(b"swd flash local --target pfw nopwrcycle\n")
        buf = b""
        start = time.time()
        while time.time() - start < FLASH_TIMEOUT_S:
            try:
                tc._sock.settimeout(5.0)
                d = tc._sock.recv(4096)
                if not d:
                    break
                buf += d
                print(f"  [{time.time()-start:5.1f}s] {d.decode(errors='replace').rstrip()}")
                if b"[PASS]" in buf or b"[FAIL]" in buf:
                    break
            except OSError:
                if time.time() - start > 15:
                    print("  (waiting for flash ...)")
        flash_ok = b"[PASS]" in buf
        r.check("SWD flash PFW", flash_ok)
        if not flash_ok:
            return False
    else:
        print("  Skipping SWD flash (--no-flash)")

    # Release PB-A — DUT KEEPALIVE latch takes over
    print("  Releasing PB-A (DUT KEEPALIVE takes over) ...")
    tc_cmd(tc, "mux release", wait=2.0)
    r.check("PB-A released", True)

    return True


# ── Phase 2: DUT GPIO continuity ─────────────────────────────────────────────

def phase_gpio_continuity(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 2: DUT GPIO continuity (CN8-9/10/11/12) ───────────────")

    # Confirm heartbeat before touching GPIOs
    print("  selftest heartbeat ...")
    resp = tc_cmd_long(tc, "selftest heartbeat", stop="Results:", timeout=HB_TIMEOUT_S)
    hb_ok = "[PASS]" in resp
    if not r.check("DUT heartbeat (pfw running)", hb_ok):
        print("  Aborting GPIO continuity — DUT not alive")
        return

    # Enter test mode
    print("  ENTER_TEST ...")
    resp = dut_cmd(tc, "ENTER_TEST", wait=1.0)
    # Accepted responses:
    #   "OK_ENTERED_TEST"                   — just entered test mode
    #   "OK TEST_MODE_ACTIVE FW_VERSION:…"  — firmware variant
    #   "ERR ALREADY_IN_TEST"               — already in test mode (valid)
    in_test = "TEST_MODE" in resp or "ENTERED_TEST" in resp or "ALREADY_IN_TEST" in resp
    if not r.check("DUT ENTER_TEST", in_test, resp):
        print("  Aborting GPIO continuity — DUT UART passthrough not responding")
        return

    # Drive all outputs HIGH, scan mux, drive all LOW, scan again
    # GPIO_SET <pin> HIGH / GPIO_CLEAR <pin> — confirmed working in pfw UART protocol
    # (GPIO_CONFIG OUTPUT_H/OUTPUT_L not supported in current pfw firmware)
    for dut_cmds, label, mv_check in [
        (lambda pin: f"GPIO_SET {pin} HIGH", "HIGH", lambda mv: mv is not None and mv > HIGH_MV_MIN),
        (lambda pin: f"GPIO_CLEAR {pin}",    "LOW",  lambda mv: mv is not None and mv < LOW_MV_MAX),
    ]:
        print(f"\n  Driving all CN8 outputs {label} ...")
        for _, pin, signal, conn in CN8_OUTPUTS:
            cmd_str = dut_cmds(pin)
            resp = dut_cmd(tc, cmd_str, wait=1.0)
            print(f"    {cmd_str}: {resp}")

        # Let the last GPIO command settle before scanning
        time.sleep(0.5)
        scan = run_mux_scan(tc, f"CN8 outputs {label}")

        for mux_key, pin, signal, conn in CN8_OUTPUTS:
            mv = scan.get(mux_key)
            ok = mv_check(mv)
            r.check(
                f"{conn} {signal}/{pin} → {label}",
                ok,
                mv_str(mv),
            )


# ── Phase 3: Monitor ADC inject+readback ─────────────────────────────────────

_ADC24_RE = re.compile(r"OK PERIPHERAL_ADC_READ \S+ (-?\d+)")


def dut_read_adc24(tc: TcConsole, name: str) -> int | None:
    """Issue PERIPHERAL_ADC_READ to DUT; return integer value or None on error.

    The LTC2498 conversion takes ~160 ms inside the DUT firmware, so we wait
    2.0 s for the UART response to arrive.
    """
    resp = dut_cmd(tc, f"PERIPHERAL_ADC_READ {name}", wait=2.0)
    m = _ADC24_RE.search(resp)
    if m:
        return int(m.group(1))
    return None


def phase_monitors(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 3: Monitor ADC inject+readback (CN8-6/7/8) ────────────")

    # Enable external ADC VREF (PC5) and VIN#3 rail (PB13)
    print("  Enabling VREF_ENA (GPIO_SET PC5 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PC5 HIGH", wait=1.0)
    print("  Enabling VIN#3 rail (GPIO_SET PB13 HIGH) ...")
    dut_cmd(tc, "GPIO_SET PB13 HIGH", wait=1.0)
    print("  Waiting 500 ms for rails and VREF to stabilise ...")
    time.sleep(0.5)

    # TIE mux snapshot — verifies pogo contact before injection
    scan = run_mux_scan(tc, "monitors baseline (no injection)")
    r.check(
        "VIN#3 monitor pogos readable (no ERR)",
        all(scan.get(k) is not None for k, _, _, _, _ in CN8_MONITORS),
        "ERR = TIE mux read failure / no pogo contact",
    )
    print(f"\n  {'Connector':<10}  {'Signal':<20}  {'TIE mV':>8}  (baseline)")
    print("  " + "─" * 48)
    for mux_key, _, _, signal, conn in CN8_MONITORS:
        mv = scan.get(mux_key)
        print(f"  {conn:<10}  {signal:<20}  {mv_str(mv):>8}")

    # Inject+readback per channel
    print()
    for mux_key, u8_ch, ltc_name, signal, conn in CN8_MONITORS:
        print(f"  {conn} {signal} — U8 ch{u8_ch} → LTC2498 {ltc_name}")

        # Baseline DUT read (no injection)
        base = dut_read_adc24(tc, ltc_name)
        print(f"    baseline: {base if base is not None else 'ERR'}")

        # Inject via U8 DAC mux (SIG=HIGH → ½-scale → ~1650 mV at pogo)
        tc_cmd(tc, f"mux select {u8_ch} 1", wait=0.5)
        time.sleep(0.5)

        injected = dut_read_adc24(tc, ltc_name)
        print(f"    injected: {injected if injected is not None else 'ERR'}")

        tc_cmd(tc, "mux release", wait=0.3)

        if base is None or injected is None:
            r.check(f"{conn} {signal} ADC inject+readback", False, "ADC read error")
        else:
            delta = injected - base
            ok = delta >= INJECT_DELTA_MIN
            r.check(
                f"{conn} {signal} ADC inject+readback",
                ok,
                f"delta={delta}  (base={base}, inj={injected}, min={INJECT_DELTA_MIN})",
            )

    # Disable VIN#3 and VREF
    print("  Disabling VIN#3 rail (GPIO_CLEAR PB13) ...")
    dut_cmd(tc, "GPIO_CLEAR PB13", wait=1.0)
    print("  Disabling VREF_ENA (GPIO_CLEAR PC5) ...")
    dut_cmd(tc, "GPIO_CLEAR PC5", wait=1.0)


# ── Phase 4: Analog switch continuity (CN8-3 / CN8-5) ───────────────────────

# (mux_key, source_pin, signal, connector)
_SW_CHANNELS: list[tuple[tuple[int, int], str, str, str]] = [
    ((1, 11), "SDA", "SDA_Rx", "CN8-3"),
    ((1, 13), "SCL", "SCL_Tx", "CN8-5"),
]

# PD8 HIGH → PB11=SDA→CN8-3, PB10=SCL→CN8-5  (I2C path)
# PD8 LOW  → PA0=RX→CN8-3,  PA1=TX→CN8-5    (UART path)
# Source: schematic — PD8 is analog switch select, HIGH activates PB11/PB10
#
# NOTE: PB10 (SCL) cannot be driven HIGH via CN8 — the TC actively clocks
# I2C SCL via CN3-1 during selftest mux (ADC128D818).  The HIGH assertion for
# PB10 will show ~0 mV due to bus contention and is skipped.
#
# NOTE: CN8-3 and CN8-5 read identically in all scans — likely shorted on the
# TIE board.  Verify with a continuity test between CN8-3 and CN8-5 pogo pins.
_SW_STATES: list[tuple[str, dict[str, str]]] = [
    ("HIGH (PD8=H)", {"SDA": "PB11", "SCL": "PB10"}),   # I2C path
    ("LOW  (PD8=L)", {"SDA": "PA0",  "SCL": "PA1"}),    # UART path
]

# PB10 HIGH is blocked by TC I2C bus contention — skip rather than FAIL
_SW_SKIP_HIGH: set[str] = {"PB10"}


def _sw_drive_and_check(
    tc: TcConsole,
    r: Results,
    pin_map: dict[str, str],
    state_label: str,
) -> None:
    """Drive source pins HIGH then LOW; read TIE mux both times."""
    for level, mv_check in [
        ("HIGH", lambda mv: mv is not None and mv > HIGH_MV_MIN),
        ("LOW",  lambda mv: mv is not None and mv < LOW_MV_MAX),
    ]:
        print(f"\n  Driving {state_label} → {level} ...")
        for _, role, signal, conn in _SW_CHANNELS:
            pin = pin_map[role]
            if level == "HIGH" and pin in _SW_SKIP_HIGH:
                # Do NOT drive HIGH — would clamp CN3-1 (TC I2C SCL), breaking selftest mux
                print(f"    GPIO_SET {pin} HIGH: SKIPPED (I2C bus contention)")
                continue
            cmd_str = f"GPIO_SET {pin} HIGH" if level == "HIGH" else f"GPIO_CLEAR {pin}"
            resp = dut_cmd(tc, cmd_str, wait=1.0)
            print(f"    {cmd_str}: {resp}")

        # Let the last GPIO command settle before scanning
        time.sleep(0.5)
        scan = run_mux_scan(tc, f"CN8-3/5 {state_label} {level}")

        for mux_key, role, signal, conn in _SW_CHANNELS:
            pin = pin_map[role]
            mv = scan.get(mux_key)
            if level == "HIGH" and pin in _SW_SKIP_HIGH:
                print(f"  [SKIP] {conn} {signal} via {pin} [{state_label}] → HIGH"
                      f"  (TC I2C bus contention on CN3-1; {mv_str(mv)})")
                continue
            r.check(
                f"{conn} {signal} via {pin} [{state_label}] → {level}",
                mv_check(mv),
                mv_str(mv),
            )


def phase_analog_switch(tc: TcConsole, r: Results) -> None:
    print("\n── Phase 4: Analog switch continuity (CN8-3/5) ─────────────────")
    print("  Switch: PD8 HIGH → I2C (PB11/PB10);  PD8 LOW → UART (PA0/PA1)  [schematic]")

    for pd8_level, pin_map in _SW_STATES:
        print(f"\n  Setting PD8 {pd8_level.split()[0]} ...")
        pd8_cmd = "GPIO_SET PD8 HIGH" if "HIGH" in pd8_level else "GPIO_CLEAR PD8"
        resp = dut_cmd(tc, pd8_cmd, wait=1.0)
        print(f"    {pd8_cmd}: {resp}")
        # Settle for analog switch to change state
        time.sleep(0.5)
        _sw_drive_and_check(tc, r, pin_map, pd8_level)

    # Leave PD8 in default (LOW) state
    dut_cmd(tc, "GPIO_CLEAR PD8", wait=1.0)


# ── Teardown ──────────────────────────────────────────────────────────────────

def teardown(tc: TcConsole) -> None:
    print("\n── Teardown ─────────────────────────────────────────────────────")
    tc_cmd(tc, "mux release", wait=2.0)
    tc_cmd(tc, "vdac off",    wait=2.0)
    tc_cmd(tc, "dut resume",  wait=2.0)
    print("  PB-A released, VDUT off, DUT auto-start resumed")


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="CN8 connector functional test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument(
        "--no-flash", action="store_true",
        help="Skip SWD flash — DUT already running pfw",
    )
    args = ap.parse_args()

    r = Results()

    with TcConsole() as tc:
        try:
            ok = phase_power_and_flash(tc, r, do_flash=not args.no_flash)
            if ok:
                phase_gpio_continuity(tc, r)
                phase_monitors(tc, r)
                phase_analog_switch(tc, r)
        finally:
            teardown(tc)

    passed = r.summary()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()

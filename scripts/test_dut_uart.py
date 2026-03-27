#!/usr/bin/env python3
"""test_dut_uart.py — Diagnose DUT UART path (TC GPIO43/44 ↔ DUT PB6/PB7).

Sends raw commands to the DUT via the TC console's `uart cmd` passthrough
and reports results.  Useful for isolating whether a `dut_enter_test` recipe
failure is a UART connectivity issue vs a PFW command handler issue.

Sequence:
  1. Connect to TC console (TCP 4242)
  2. Pause DUT auto-start
  3. Check DUT presence (ADC threshold)
  4. Power DUT: VDUT 3300 mV + PB-A assert
  5. Wait for PFW to boot
  6. Send `uart cmd STATUS`     → expects "OK <state> UPTIME:<n>"
  7. Send `uart cmd ENTER_TEST` → expects "OK TEST_MODE_ACTIVE FW_VERSION:..."
  8. Send `uart cmd VERSION`    → expects "OK FW_VERSION:..."
  9. Send `uart cmd EXIT_TEST`  → expects "OK EXITING_TEST"
 10. Teardown: mux release, vdac off, resume auto-start

Exit codes:
  0 = all UART commands got expected responses
  1 = one or more commands got no response (UART path issue)
  2 = DUT not present or TC unreachable

Usage:
    python3 scripts/test_dut_uart.py [--host 10.0.0.244] [--boot-wait 2.0]
    python3 scripts/test_dut_uart.py --skip-power   # DUT already powered
"""

import argparse
import re
import sys
import time

# Allow running from repo root or scripts/
sys.path.insert(0, __file__.rsplit("/", 1)[0] if "/" in __file__ else ".")
from tc_console import TcConsole

DEFAULT_HOST      = "10.0.0.244"
DEFAULT_BOOT_WAIT = 2.0  # seconds after PB-A for PFW to init UART


def parse_uart_response(tc_output: str) -> str:
    """Extract the DUT response from `uart cmd` output.

    The TC prints whatever the DUT sends back.  If it gets nothing,
    it prints "(no response)".  We strip TC prompt noise and return
    the meaningful line.
    """
    for line in tc_output.splitlines():
        line = line.strip()
        # Skip TC prompt lines and echo
        if not line or line.startswith("g3-tc|") or line.startswith("uart cmd"):
            continue
        # Skip the monitor header
        if line.startswith("===") or "waiting" in line.lower():
            continue
        return line
    return "(empty)"


def run_uart_cmd(tc: TcConsole, cmd: str, timeout: float = 4.0) -> str:
    """Send `uart cmd <cmd>` via TC console, return parsed DUT response."""
    raw = tc.cmd(f"uart cmd {cmd}", wait=timeout)
    return parse_uart_response(raw)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Diagnose DUT UART path via TC console `uart cmd`",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"TC IP (default: {DEFAULT_HOST})")
    parser.add_argument("--boot-wait", type=float, default=DEFAULT_BOOT_WAIT,
                        help=f"Seconds to wait for PFW boot (default: {DEFAULT_BOOT_WAIT})")
    parser.add_argument("--skip-power", action="store_true",
                        help="Skip DUT power-up (assume already powered)")
    parser.add_argument("--heartbeat-only", action="store_true",
                        help="Only check heartbeat (confirm PFW is running), skip UART cmds")
    parser.add_argument("--probe", type=float, default=0, metavar="SECS",
                        help="Probe mode: TC sends PING at 7Hz while monitoring RX for SECS. "
                             "PFW UART HB is 3Hz, TC TX is 7Hz — different rates for scope ID.")
    args = parser.parse_args()

    print(f"=== DUT UART Diagnostic — TC @ {args.host}:4242 ===\n")

    # ── 1. Connect ───────────────────────────────────────────────────────
    print("[1] Connecting to TC console...")
    tc = TcConsole(ip=args.host)
    try:
        tc.connect()
        print("    Connected\n")
    except Exception as e:
        print(f"    FAILED: {e}")
        sys.exit(2)

    # ── 2. Pause auto-start ──────────────────────────────────────────────
    print("[2] Pausing DUT auto-start")
    tc.cmd("dut pause", wait=1)

    # ── 3. DUT presence ──────────────────────────────────────────────────
    print("[3] Checking DUT presence")
    resp = tc.cmd("dut sample", wait=3)
    adc_mv = -1
    for line in resp.splitlines():
        m = re.search(r"(\d+)\s*mV", line)
        if m:
            adc_mv = int(m.group(1))
            break

    present = 0 < adc_mv < 2000
    print(f"    ADC = {adc_mv} mV  -> {'PRESENT' if present else 'ABSENT'}")
    if not present:
        print("    ERROR: DUT not seated")
        tc.cmd("dut resume", wait=1)
        tc.close()
        sys.exit(2)

    # ── 4. Power DUT ─────────────────────────────────────────────────────
    if not args.skip_power:
        print(f"\n[4] Powering DUT: VDUT 3300 mV + PB-A assert")
        tc.cmd("vdac_voltage both 3300", wait=2)
        tc.cmd("mux select 0 0", wait=1)
        print(f"    Waiting {args.boot_wait:.1f}s for PFW to boot...")
        time.sleep(args.boot_wait)
    else:
        print("\n[4] Skipping power-up (--skip-power)")

    # ── 5. Heartbeat check (confirm PFW is running) ────────────────────
    print("\n[5] Checking PFW heartbeat (selftest heartbeat)")
    hb_resp = tc.cmd("selftest heartbeat", wait=8)
    hb_pass = "[PASS]" in hb_resp and "heartbeat" in hb_resp.lower()
    print(f"    {'PASS' if hb_pass else 'FAIL'} — PFW heartbeat")
    for line in hb_resp.splitlines():
        line = line.strip()
        if any(k in line for k in ("[PASS]", "[FAIL]", "heartbeat", "Heartbeat")):
            print(f"    {line}")
    if not hb_pass:
        print("    PFW not running — UART test would be meaningless.")

    if args.heartbeat_only:
        print("\n    (--heartbeat-only: skipping UART commands)")
        if not args.skip_power:
            tc.cmd("mux release", wait=1)
            tc.cmd("vdac off", wait=1)
        tc.cmd("dut resume", wait=1)
        tc.close()
        sys.exit(0 if hb_pass else 1)

    # ── 5b. Probe mode ──────────────────────────────────────────────────
    if args.probe > 0:
        secs = int(args.probe)
        print(f"\n[5b] Probe mode: {secs}s — running `uart probe {secs}`")
        print("     PFW TX (DUT PB6 → TC GPIO44): 3 Hz 'HB <n> <ms>'")
        print("     TC  TX (TC GPIO43 → DUT PB7): 7 Hz 'PING <n>'")
        print("     Use scope on pogo pins to identify signals by rate.\n")
        resp = tc.cmd(f"uart probe {secs}", wait=secs + 3)
        for line in resp.splitlines():
            line = line.strip()
            if line and not line.startswith("g3-tc|"):
                print(f"    {line}")
        if not args.skip_power:
            tc.cmd("mux release", wait=1)
            tc.cmd("vdac off", wait=1)
        tc.cmd("dut resume", wait=1)
        tc.close()
        sys.exit(0)

    # ── 6. UART commands ─────────────────────────────────────────────────
    commands = [
        ("STATUS",     "OK",  "Basic UART echo — any response means the path works"),
        ("ENTER_TEST", "OK TEST_MODE_ACTIVE", "Enter test mode"),
        ("VERSION",    "OK FW_VERSION:", "Read PFW version (requires test mode)"),
        ("EXIT_TEST",  "OK EXITING_TEST", "Exit test mode"),
    ]

    results: list[tuple[str, str, bool]] = []
    print(f"\n[6] Sending UART commands via TC `uart cmd`\n")

    for cmd, expect, desc in commands:
        resp = run_uart_cmd(tc, cmd)
        ok = resp.startswith(expect) if expect else bool(resp)
        status = "PASS" if ok else "FAIL"
        results.append((cmd, resp, ok))
        print(f"    [{status}] uart cmd {cmd}")
        print(f"           -> {resp}")
        print(f"           ({desc})\n")

    # ── 7. Teardown ──────────────────────────────────────────────────────
    if not args.skip_power:
        print("[7] Teardown: releasing power, resuming auto-start")
        tc.cmd("mux release", wait=1)
        tc.cmd("vdac off", wait=1)
    tc.cmd("dut resume", wait=1)
    tc.close()

    # ── Summary ──────────────────────────────────────────────────────────
    pass_count = sum(1 for _, _, ok in results if ok)
    total = len(results)
    all_no_response = all("no response" in r.lower() or r == "(empty)" for _, r, _ in results)

    print("=" * 55)
    print(f"  Results: {pass_count}/{total} commands passed\n")

    if pass_count == total:
        print("  UART path OK — DUT PFW responds to all commands.")
        print("  If recipe dut_enter_test still fails, check timing")
        print("  (boot delay) or UART driver install/delete sequencing.")
    elif all_no_response:
        print("  NO RESPONSE on any command — UART path is broken.")
        print("  Check:")
        print("    - TC GPIO43 (TX) -> pogo -> DUT PB7 (USART1 RX)")
        print("    - DUT PB6 (USART1 TX) -> pogo -> TC GPIO44 (RX)")
        print("    - PFW actually booted (run: selftest heartbeat)")
        print("    - No other driver holding UART_NUM_1")
    else:
        print("  Partial responses — PFW may be in an unexpected state.")
        print("  Try power-cycling (remove --skip-power) and rerun.")

    sys.exit(0 if pass_count == total else 1)


if __name__ == "__main__":
    main()

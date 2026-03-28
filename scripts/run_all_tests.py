#!/usr/bin/env python3
"""Run the full TC + PFW regression suite, then optionally the live recipe.

Test order:
  Phase 1 — TC-side regressions (no DUT required):
    1. test_tc_commands.py    — TC console commands
    2. test_recipe_json.py    — JSON parse/serialize/NVS round-trip
    3. test_recipe_engine.py  — recipe execution engine
    4. test_recipe_storage.py — SPIFFS recipe lifecycle
    5. test_recipe_dcmd.py    — MQTT recipe management DCMDs

  DUT detection (UART ENTER_TEST ping):
    → If no DUT: Phase 2 and recipe are skipped; suite exits 0.

  Phase 2 — PFW regressions (DUT required):
    6. test_gpio_cmds.py  — GPIO_SET / GPIO_CLEAR / PIN_READ for all 25 pins
    7. test_adc_scan.py   — LTC2498 16-channel ADC scan

  Recipe run — only if Phase 1 + Phase 2 all pass AND DUT detected:
    8. sm start → capture step results via MQTT → print pass/fail table

Usage:
    python3 scripts/run_all_tests.py [--tc-ip IP] [--broker IP]
"""

import json
import os
import socket
import subprocess
import sys
import threading
import time

import paho.mqtt.client as mqtt

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
PFW_SCRIPTS_DIR = os.path.normpath(
    os.path.join(SCRIPTS_DIR, "../../dut-firmware/scripts")
)

# Pull TC connection settings from tc_config / local_config
sys.path.insert(0, SCRIPTS_DIR)
from tc_config import HOST, PORT, BROKER, TC_SERIAL as NODE

GROUP   = "SensitMfg"
CHANNEL = 0

# ── Helpers ───────────────────────────────────────────────────────────────────

GREEN = "\033[32m"
RED   = "\033[31m"
RESET = "\033[0m"
BOLD  = "\033[1m"

def _pass(label: str) -> str:
    return f"[{GREEN}PASS{RESET}] {label}"

def _fail(label: str) -> str:
    return f"[{RED}FAIL{RESET}] {label}"

def _section(title: str) -> None:
    print(f"\n{'─' * 60}")
    print(f"{BOLD}{title}{RESET}")
    print(f"{'─' * 60}")


def _tc_cmd(cmd: str, wait: float = 2.0) -> str:
    """Send a command to the TC TCP console and return the response."""
    s = socket.socket()
    s.settimeout(5)
    s.connect((HOST, PORT))
    time.sleep(0.2)
    s.settimeout(0.5)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
    s.sendall((cmd + "\r\n").encode())
    out = b""
    deadline = time.time() + wait
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
            if chunk:
                out += chunk
        except OSError:
            pass
    s.close()
    return out.decode("utf-8", errors="replace")


def _run_script(label: str, script_path: str, extra_args: list[str] | None = None) -> tuple[bool, float]:
    """Run a Python test script as a subprocess. Returns (passed, elapsed_s)."""
    cmd = [sys.executable, script_path] + (extra_args or [])
    start = time.time()
    proc = subprocess.run(cmd, timeout=300)
    elapsed = time.time() - start
    ok = proc.returncode == 0
    status = _pass(label) if ok else _fail(label)
    print(f"\n  {status}  ({elapsed:.1f}s)")
    return ok, elapsed


# ── DUT detection ─────────────────────────────────────────────────────────────

def _detect_dut() -> bool:
    """Return True if a DUT is present and responsive on the pogo pins.

    Sends ENTER_TEST via UART bridge and looks for TEST_MODE_ACTIVE.
    Immediately sends EXIT_TEST on success.
    """
    try:
        _tc_cmd("mux release", wait=0.5)
        resp = _tc_cmd("uart cmd ENTER_TEST", wait=2.0)
        if "TEST_MODE_ACTIVE" in resp:
            _tc_cmd("uart cmd EXIT_TEST", wait=1.0)
            return True
        return False
    except OSError:
        return False


# ── Recipe run ────────────────────────────────────────────────────────────────

def _run_recipe(timeout: int = 90) -> tuple[bool, int, int]:
    """Trigger sm start and capture step results via MQTT.

    Returns (outcome_pass, steps_passed, steps_total).
    """
    step_results: list[dict] = []
    final_result: dict = {}
    done = threading.Event()

    def on_connect(client: mqtt.Client, userdata, flags, rc) -> None:
        client.subscribe(f"spBv1.0/{GROUP}/DDATA/#")

    def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage) -> None:
        try:
            obj = json.loads(msg.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        t = obj.get("type", "")
        if t == "step_result":
            step_results.append(obj)
        elif t == "result":
            final_result.update(obj)
            done.set()

    client = mqtt.Client()  # type: ignore[call-arg]
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, 1883, 10)
    client.loop_start()
    time.sleep(1)

    print("  Triggering sm start ...")
    _tc_cmd("sm start", wait=0.5)
    print(f"  Waiting for result (up to {timeout}s) ...")
    done.wait(timeout=timeout)

    client.loop_stop()
    client.disconnect()

    print(f"\n  {'Step':<35} Result")
    print(f"  {'-' * 43}")
    for sr in step_results:
        sid    = sr.get("step_id", sr.get("id", "?"))
        passed = sr.get("passed", sr.get("pass", False))
        mark   = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
        print(f"  {sid:<35} {mark}")

    outcome_str = final_result.get("outcome", "unknown")
    dur         = final_result.get("duration_ms", 0)
    total       = len(step_results)
    passes      = sum(1 for sr in step_results if sr.get("passed", sr.get("pass")))
    outcome_ok  = outcome_str.lower() == "pass"

    marker = _pass if outcome_ok else _fail
    print(f"\n  {marker(f'Recipe: {outcome_str.upper()}  {passes}/{total}  {dur}ms')}")

    return outcome_ok, passes, total


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    phase1_tests = [
        ("TC Commands",      os.path.join(SCRIPTS_DIR,     "test_tc_commands.py")),
        ("Recipe JSON",      os.path.join(SCRIPTS_DIR,     "test_recipe_json.py")),
        ("Recipe Engine",    os.path.join(SCRIPTS_DIR,     "test_recipe_engine.py")),
        ("Recipe Storage",   os.path.join(SCRIPTS_DIR,     "test_recipe_storage.py")),
        ("Recipe DCMD",      os.path.join(SCRIPTS_DIR,     "test_recipe_dcmd.py")),
    ]
    phase2_tests = [
        ("PFW GPIO Commands", os.path.join(PFW_SCRIPTS_DIR, "test_gpio_cmds.py")),
        ("PFW ADC Scan",      os.path.join(PFW_SCRIPTS_DIR, "test_adc_scan.py")),
    ]

    print("=" * 60)
    print(f"{BOLD}TC + PFW Full Regression Suite{RESET}")
    print("=" * 60)

    results: list[tuple[str, bool, float]] = []
    total_time = 0.0

    # ── Phase 1: TC-side ──────────────────────────────────────────────────
    _section("Phase 1 — TC Regressions (no DUT required)")
    phase1_ok = True
    for name, path in phase1_tests:
        print(f"\n▶ {name}  ({os.path.basename(path)})")
        ok, elapsed = _run_script(name, path)
        results.append((name, ok, elapsed))
        total_time += elapsed
        if not ok:
            phase1_ok = False
        time.sleep(1)

    if not phase1_ok:
        _print_summary(results, total_time, dut_detected=False, recipe_result=None)
        print(f"\n{RED}Phase 1 failed — skipping DUT tests and recipe.{RESET}")
        sys.exit(1)

    # ── DUT detection ─────────────────────────────────────────────────────
    _section("DUT Detection")
    print("\n  Probing DUT via UART ENTER_TEST ...")
    dut_present = _detect_dut()
    if dut_present:
        print(f"  {GREEN}DUT detected{RESET} — PFW tests and recipe will run.")
    else:
        print(f"  {RED}No DUT detected{RESET} — skipping Phase 2 and recipe run.")
        _print_summary(results, total_time, dut_detected=False, recipe_result=None)
        sys.exit(0)

    # ── Phase 2: PFW ──────────────────────────────────────────────────────
    _section("Phase 2 — PFW Regressions (DUT required)")
    phase2_ok = True
    for name, path in phase2_tests:
        print(f"\n▶ {name}  ({os.path.basename(path)})")
        ok, elapsed = _run_script(name, path)
        results.append((name, ok, elapsed))
        total_time += elapsed
        if not ok:
            phase2_ok = False
        time.sleep(1)

    if not phase2_ok:
        _print_summary(results, total_time, dut_detected=True, recipe_result=None)
        print(f"\n{RED}Phase 2 failed — skipping recipe run.{RESET}")
        sys.exit(1)

    # ── Recipe run ────────────────────────────────────────────────────────
    _section("Recipe Run")
    recipe_ok, recipe_pass, recipe_total = _run_recipe(timeout=120)
    recipe_result = (recipe_ok, recipe_pass, recipe_total)

    _print_summary(results, total_time, dut_detected=True, recipe_result=recipe_result)
    sys.exit(0 if recipe_ok else 1)


def _print_summary(
    results: list[tuple[str, bool, float]],
    total_time: float,
    dut_detected: bool,
    recipe_result: tuple[bool, int, int] | None,
) -> None:
    print(f"\n{'=' * 60}")
    print(f"{BOLD}Summary{RESET}")
    print(f"{'=' * 60}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = sum(1 for _, ok, _ in results if not ok)

    for name, ok, elapsed in results:
        status = _pass(name) if ok else _fail(name)
        print(f"  {status:<45}  {elapsed:6.1f}s")

    if not dut_detected:
        print(f"  [{'─' * 4}] DUT not detected — PFW tests and recipe skipped")
    elif recipe_result is not None:
        ok, passes, total = recipe_result
        label = f"Recipe: {passes}/{total} steps"
        status = _pass(label) if ok else _fail(label)
        print(f"  {status}")

    print(f"\n  Suites: {passed}/{len(results)} passed, {failed} failed")
    print(f"  Total time: {total_time:.0f}s")
    print("=" * 60)


if __name__ == "__main__":
    main()

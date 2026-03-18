#!/usr/bin/env python3
"""Run the full TC regression suite — all test scripts in sequence.

Usage:
    python3 scripts/run_all_tests.py [--skip-sm]  # skip sm start (saves ~10s)

Test order:
    1. test_tc_commands.py    — all TC console commands (comprehensive)
    2. test_recipe_json.py    — JSON parse/serialize/NVS round-trip
    3. test_recipe_engine.py  — recipe execution engine
    4. test_recipe_storage.py — SPIFFS recipe lifecycle
    5. test_recipe_dcmd.py    — MQTT recipe management DCMDs
"""

import subprocess
import sys
import time

SCRIPTS_DIR = __file__.rsplit("/", 1)[0] if "/" in __file__ else "."

TESTS = [
    ("TC Commands",      "test_tc_commands.py"),
    ("Recipe JSON",      "test_recipe_json.py"),
    ("Recipe Engine",    "test_recipe_engine.py"),
    ("Recipe Storage",   "test_recipe_storage.py"),
    ("Recipe DCMD",      "test_recipe_dcmd.py"),
]


def main():
    skip_sm = "--skip-sm" in sys.argv

    print("=" * 60)
    print("TC Full Regression Suite")
    print("=" * 60)

    results = []
    total_time = 0

    for name, script in TESTS:
        print(f"\n{'─' * 60}")
        print(f"▶ {name}  ({script})")
        print(f"{'─' * 60}\n")

        cmd = [sys.executable, f"{SCRIPTS_DIR}/{script}"]
        if skip_sm and script == "test_tc_commands.py":
            # test_tc_commands doesn't have --skip-sm but sm start is embedded
            pass  # run as-is

        start = time.time()
        proc = subprocess.run(cmd, timeout=300)
        elapsed = time.time() - start
        total_time += elapsed

        ok = proc.returncode == 0
        results.append((name, ok, elapsed))

        status = "\033[32mPASS\033[0m" if ok else "\033[31mFAIL\033[0m"
        print(f"\n  [{status}] {name}  ({elapsed:.1f}s)")

        if not ok:
            print(f"  ⚠ {script} returned exit code {proc.returncode}")

        # Brief pause between suites to let TC settle
        time.sleep(2)

    # ── Summary ──────────────────────────────────────────────────────────
    print(f"\n{'=' * 60}")
    print(f"Full Suite Results  ({total_time:.0f}s total)")
    print(f"{'=' * 60}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = sum(1 for _, ok, _ in results if not ok)

    for name, ok, elapsed in results:
        status = "\033[32mPASS\033[0m" if ok else "\033[31mFAIL\033[0m"
        print(f"  [{status}] {name:<25s}  {elapsed:6.1f}s")

    print(f"\n  Suites: {passed}/{len(results)} passed, {failed} failed")
    print(f"{'=' * 60}")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()

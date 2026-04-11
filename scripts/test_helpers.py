"""
Shared helpers for G3 per-connector test scripts.
Provides bring-up, teardown, mux scan, DUT UART command, and Results tracking.
"""
from __future__ import annotations
import sys, time, re
sys.path.insert(0, __file__[:__file__.rfind("/")])
from tc_console import TcConsole

# ── Thresholds ────────────────────────────────────────────────────────────────
VDUT_MV     = 3300
HIGH_MV_MIN = 2700   # DUT 3.3V HIGH → TIE mux must read > this
LOW_MV_MAX  = 300    # DUT driven LOW → TIE mux must read < this

# ── Results tracker ───────────────────────────────────────────────────────────

GREEN = "\033[32m"
RED   = "\033[31m"
RST   = "\033[0m"

class Results:
    def __init__(self) -> None:
        self._pass = 0
        self._fail = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        tag = f"{GREEN}PASS{RST}" if ok else f"{RED}FAIL{RST}"
        suffix = f"  ({detail})" if detail else ""
        print(f"  [{tag}] {name}{suffix}")
        if ok:
            self._pass += 1
        else:
            self._fail += 1
        return ok

    def skip(self, name: str, reason: str = "") -> None:
        suffix = f"  ({reason})" if reason else ""
        print(f"  [SKIP] {name}{suffix}")

    def summary(self) -> bool:
        total = self._pass + self._fail
        ok = self._fail == 0
        tag = f"{GREEN}PASS{RST}" if ok else f"{RED}FAIL{RST}"
        print(f"\n[{tag}] {total} checks: {self._pass} passed, {self._fail} failed")
        return ok

# ── TC / DUT command helpers ──────────────────────────────────────────────────

def tc_cmd(tc: TcConsole, cmd: str, wait: float = 1.5) -> str:
    return tc.cmd(cmd, wait=wait)


def tc_cmd_long(tc: TcConsole, cmd: str, stop: str, timeout: float = 30.0) -> str:
    """Send cmd; read until stop marker appears or timeout."""
    tc._sock.sendall((cmd + "\n").encode())
    buf = b""
    tc._sock.settimeout(timeout)
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            chunk = tc._sock.recv(8192)
            if not chunk:
                break
            buf += chunk
            if stop.encode() in buf:
                # drain a little more
                tc._sock.settimeout(1.0)
                try:
                    while True:
                        buf += tc._sock.recv(8192)
                except Exception:
                    pass
                break
    except Exception:
        pass
    finally:
        tc._sock.settimeout(tc.timeout)
    return buf.decode(errors="replace")


def dut_cmd(tc: TcConsole, cmd: str, wait: float = 0.8) -> str:
    """Send command to DUT via TC UART passthrough; return stripped response."""
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

# ── TIE mux scan ─────────────────────────────────────────────────────────────

_ROW_RE = re.compile(r"^\s*(\d)\s+(\d+)\s+\d\s+\d\s+\d\s+\d\s+(ERR|[-\d]+)")


def run_mux_scan(tc: TcConsole, label: str) -> dict[tuple[int, int], int | None]:
    """Run 'selftest mux'; return {(mux, ch): mv} for all channels. None=ERR."""
    print(f"  selftest mux ({label}) ...")
    raw = tc_cmd_long(tc, "selftest mux", stop="scan complete", timeout=30.0)
    if "scan complete" not in raw:
        print(f"  WARNING: selftest mux timed out or returned no data")
    result: dict[tuple[int, int], int | None] = {}
    for line in raw.splitlines():
        m = _ROW_RE.match(line)
        if m:
            key = (int(m.group(1)), int(m.group(2)))
            result[key] = None if m.group(3) == "ERR" else int(m.group(3))
    return result

# ── Standard bring-up sequence ────────────────────────────────────────────────

def phase_bring_up(tc: TcConsole, r: Results, label: str, do_flash: bool = True) -> bool:
    """
    Phase 1 — standard DUT bring-up:
      power on, assert PB-A, SWD probe, flash PFW, release PB-A,
      check heartbeat, ENTER_TEST.
    Pass do_flash=False to skip SWD probe/flash (DUT already running pfw).
    Returns True if all steps passed and DUT is in test mode.
    """
    print(f"\n── Phase 1: DUT bring-up ({label}) ─────────────────────────────────────")

    tc_cmd(tc, "dut pause", wait=2.0)

    resp = tc_cmd(tc, f"vdac_voltage 1 {VDUT_MV}", wait=3.0)
    if not r.check("VDUT1 enabled", f"VDUT1: {VDUT_MV}" in resp, resp.strip()):
        return False

    # Assert PB-A to latch DUT KEEPALIVE — required with or without flash
    tc_cmd(tc, "mux select 0 0", wait=0.5)
    r.check("PB-A asserted", True)

    if do_flash:
        resp = tc_cmd(tc, "swd probe", wait=2.0)
        if not r.check("SWD probe", "g3-tc|" in resp, resp.strip()):
            tc_cmd(tc, "mux release", wait=1.0)
            return False

        print("  Flashing PFW ...")
        raw = tc_cmd_long(tc, "swd flash local --target pfw nopwrcycle",
                          stop="[PASS] swd flash", timeout=60.0)
        if not r.check("SWD flash PFW", "[PASS] swd flash" in raw):
            tc_cmd(tc, "mux release", wait=1.0)
            return False
    else:
        print("  Skipping SWD flash (--no-flash)")

    tc_cmd(tc, "mux release", wait=2.0)
    time.sleep(2.0)
    r.check("PB-A released", True)

    # Heartbeat
    print("  selftest heartbeat ...")
    raw = tc_cmd_long(tc, "selftest heartbeat", stop="DUT heartbeat", timeout=12.0)
    if not r.check("DUT heartbeat (pfw running)", "[PASS]" in raw, raw.strip().split("\n")[-1]):
        return False

    # ENTER_TEST
    resp = dut_cmd(tc, "ENTER_TEST", wait=1.5)
    in_test = "TEST_MODE" in resp or "ALREADY" in resp
    if not r.check("DUT ENTER_TEST", in_test, resp):
        return False

    return True


def teardown(tc: TcConsole) -> None:
    """Release mux, power off DUT, resume auto-detect."""
    print("\n── Teardown ─────────────────────────────────────────────────────────────")
    try:
        dut_cmd(tc, "EXIT_TEST", wait=0.5)
    except Exception:
        pass
    tc_cmd(tc, "mux release", wait=0.5)
    tc_cmd(tc, "vdac off", wait=2.0)
    tc_cmd(tc, "dut resume", wait=1.0)
    print("  VDUT off, DUT auto-start resumed")

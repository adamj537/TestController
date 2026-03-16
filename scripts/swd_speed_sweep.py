#!/usr/bin/env python3
"""SWD speed sweep — find fastest reliable bit-bang rate for DUT flash.

Connects to TCC carrier via TCP console (port 4242), sets SWD half-period,
runs a full flash cycle (erase + program), then reads back first 8 words
and compares against the known binary for data integrity verification.

Usage:
    python3 swd_speed_sweep.py [--host 10.0.0.244] [--url http://10.0.0.246:8080/dut_firmware.bin]
"""

import socket
import struct
import time
import re
import sys
import argparse

HOST = "10.0.0.244"
PORT = 4242
DUT_FW_URL = "http://10.0.0.246:8080/dut_firmware.bin"
DUT_FW_PATH = "/home/cbasta/G3-MB-Tester/embedded/dut-firmware/.pio/build/g3-dut/firmware.bin"
RECV_TIMEOUT = 90

# Expected first 8 words of DUT firmware (read from binary)
def load_expected_words(path: str, n: int = 8) -> list[int]:
    with open(path, "rb") as f:
        data = f.read(n * 4)
    return list(struct.unpack(f"<{n}I", data))

EXPECTED_WORDS = load_expected_words(DUT_FW_PATH)


def send_cmd(sock: socket.socket, cmd: str, timeout: float = RECV_TIMEOUT) -> str:
    sock.sendall((cmd + "\n").encode())
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock.settimeout(max(0.1, deadline - time.time()))
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            text = buf.decode(errors="replace")
            if re.search(r"g3-tc\|[^>]+>\s*$", text):
                return text
        except socket.timeout:
            continue
    return buf.decode(errors="replace")


def verify_flash(sock: socket.socket) -> tuple[bool, str]:
    """Read back first 8 words of DUT flash and compare against expected."""
    out = send_cmd(sock, "swd read 08000000 8", timeout=30)
    read_words = []
    for m in re.finditer(r"0x([0-9A-Fa-f]{8}):\s+0x([0-9A-Fa-f]{8})", out):
        read_words.append(int(m.group(2), 16))

    if len(read_words) < 8:
        return False, f"readback got only {len(read_words)}/8 words"

    mismatches = []
    for i, (exp, got) in enumerate(zip(EXPECTED_WORDS, read_words)):
        if exp != got:
            mismatches.append(f"word[{i}] exp=0x{exp:08X} got=0x{got:08X}")

    if mismatches:
        return False, "; ".join(mismatches[:3])
    return True, "8/8 words match"


def run_flash_test(host: str, port: int, half_us: int, url: str) -> dict:
    result = {
        "half_us": half_us,
        "khz": 1000 // (2 * half_us) if half_us > 0 else 9999,
        "success": False,
        "verified": False,
        "uid": None,
        "flash_time_s": None,
        "verify_time_s": None,
        "total_time_s": None,
        "error": None,
    }

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    try:
        sock.connect((host, port))
        time.sleep(1)
        sock.recv(4096)

        t_start = time.time()

        # Set speed
        out = send_cmd(sock, f"swd speed {half_us}", timeout=5)
        if "half-period" not in out:
            result["error"] = "speed command failed"
            return result

        # Read UID
        out = send_cmd(sock, "swd uid", timeout=30)
        uid_match = re.search(r"UID96:\s+([0-9A-Fa-f]{24})", out)
        if uid_match:
            result["uid"] = uid_match.group(1)
        elif "FAIL" in out:
            result["error"] = "UID read failed at this speed"
            return result

        # Flash DUT
        t_flash_start = time.time()
        out = send_cmd(sock, f"swd flash {url}", timeout=RECV_TIMEOUT)
        t_flash_done = time.time()
        result["flash_time_s"] = round(t_flash_done - t_flash_start, 2)

        if "[PASS]" in out and "swd flash complete" in out:
            result["success"] = True
        elif "FAIL" in out or "failed" in out.lower():
            err_lines = [l.strip() for l in out.split("\n") if "FAIL" in l or "fail" in l.lower()]
            result["error"] = err_lines[0] if err_lines else "flash failed"
            result["total_time_s"] = round(time.time() - t_start, 2)
            return result
        else:
            lines = [l.strip() for l in out.strip().split("\n") if l.strip()]
            result["error"] = f"unknown: {lines[-2:]}"
            result["total_time_s"] = round(time.time() - t_start, 2)
            return result

        # Verify readback
        t_verify_start = time.time()
        verified, detail = verify_flash(sock)
        t_verify_done = time.time()
        result["verify_time_s"] = round(t_verify_done - t_verify_start, 2)
        result["verified"] = verified
        if not verified:
            result["success"] = False
            result["error"] = f"VERIFY FAIL: {detail}"

        result["total_time_s"] = round(time.time() - t_start, 2)
        return result

    except Exception as e:
        result["error"] = str(e)
        return result
    finally:
        sock.close()


def print_results_table(results: list[dict]) -> None:
    print()
    hdr = f"{'half_us':>7} {'kHz':>6} {'Flash':>8} {'Verify':>8} {'Result':>8} {'Flash(s)':>9} {'Vrfy(s)':>8} {'Total(s)':>9} {'UID':>26} {'Error'}"
    print(hdr)
    print("-" * len(hdr) + "-" * 20)
    best = None
    for r in results:
        status = "PASS" if r["success"] else "FAIL"
        vfy = "OK" if r["verified"] else "FAIL"
        flash = f"{r['flash_time_s']:.2f}" if r["flash_time_s"] is not None else "-"
        vrfy_t = f"{r['verify_time_s']:.2f}" if r.get("verify_time_s") is not None else "-"
        total = f"{r['total_time_s']:.2f}" if r["total_time_s"] is not None else "-"
        uid = r["uid"] or "-"
        err = r.get("error") or ""
        print(f"{r['half_us']:>7} {r['khz']:>6} {status:>8} {vfy:>8} {status:>8} {flash:>9} {vrfy_t:>8} {total:>9} {uid:>26} {err}")
        if r["success"] and r["verified"] and (best is None or r["half_us"] < best["half_us"]):
            best = r

    if best:
        print()
        print(f"=== BEST: half_us={best['half_us']} (~{best['khz']} kHz)  "
              f"flash={best['flash_time_s']:.2f}s  verify={best['verify_time_s']:.2f}s  "
              f"total={best['total_time_s']:.2f}s ===")


def main():
    parser = argparse.ArgumentParser(description="SWD speed sweep with readback verify")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--url", default=DUT_FW_URL)
    parser.add_argument("--mode", choices=["sweep", "single"], default="sweep")
    parser.add_argument("--half-us", type=int, default=10)
    parser.add_argument("--speeds", type=str, default="10,5,3,2,1",
                        help="Comma-separated half_us values to sweep (slow to fast)")
    args = parser.parse_args()

    results = []

    if args.mode == "single":
        r = run_flash_test(args.host, args.port, args.half_us, args.url)
        results.append(r)

    elif args.mode == "sweep":
        speeds = [int(x) for x in args.speeds.split(",")]
        for half_us in speeds:
            khz = 1000 // (2 * half_us) if half_us > 0 else 9999
            print(f"\n--- Testing half_us={half_us} (~{khz} kHz) ---")
            r = run_flash_test(args.host, args.port, half_us, args.url)
            results.append(r)
            status = "PASS+VERIFIED" if r["success"] and r["verified"] else "FAIL"
            flash_t = f"{r['flash_time_s']:.2f}s" if r['flash_time_s'] else "-"
            print(f"  {status}  flash={flash_t}  error={r.get('error', '')}")

            if not r["success"]:
                print(f"  FAILED at {half_us}µs — stopping sweep")
                break

            time.sleep(2)

    print_results_table(results)


if __name__ == "__main__":
    main()

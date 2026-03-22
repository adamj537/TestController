#!/usr/bin/env python3
"""Visualize the ESP32-S3 flash partition layout with utilization analysis.

Usage:
    python3 scripts/flash_map.py [--partitions <csv>] [--tc-fw <bin>] [--pfw <bin>] [--prod-fw <bin>]

Defaults assume the script is run from the tester-client directory.
"""

import argparse
import csv
import os
import sys


def parse_size(s: str) -> int:
    """Parse a size string like '2M', '0xE0000', '384K' into bytes."""
    s = s.strip()
    if s.upper().endswith("M"):
        return int(s[:-1]) * 1024 * 1024
    if s.upper().endswith("K"):
        return int(s[:-1]) * 1024
    return int(s, 0)


def file_size(path: str) -> int | None:
    """Return file size in bytes, or None if not found."""
    try:
        return os.path.getsize(path)
    except OSError:
        return None


def parse_partitions(csv_path: str) -> list[dict]:
    """Parse an ESP-IDF partition table CSV."""
    parts = []
    with open(csv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [f.strip() for f in line.split(",")]
            if len(fields) < 5:
                continue
            name, ptype, subtype, offset, size = fields[:5]
            parts.append({
                "name": name,
                "type": ptype,
                "subtype": subtype,
                "offset": int(offset, 0),
                "size": parse_size(size),
            })
    return parts


def main() -> None:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    repo_root = os.path.dirname(os.path.dirname(project_dir))

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--partitions", default=os.path.join(project_dir, "partitions_ota.csv"))
    p.add_argument("--tc-fw", default=os.path.join(project_dir, ".pio/build/esp32-Devkit/firmware.bin"))
    p.add_argument("--pfw", default=os.path.join(project_dir, ".pio/build/g3-dut/firmware.bin"),
                   help="DUT PFW (test-mode firmware)")
    p.add_argument("--prod-fw", default=os.path.join(repo_root, "reference-materials/firmware/Image/G3_01_00_46_FACT.bin"),
                   help="Sensit production firmware binary")
    p.add_argument("--flash-size", type=str, default="8M")
    args = p.parse_args()

    total_flash = parse_size(args.flash_size)
    parts = parse_partitions(args.partitions)

    tc_fw_size = file_size(args.tc_fw)
    pfw_size = file_size(args.pfw)
    prod_fw_size = file_size(args.prod_fw)

    # Known usage per partition (bytes).  None = fixed/system.
    HDR = 32  # dut_fw_hdr_t
    usage: dict[str, int | None] = {
        "nvs":      7300,       # estimated from NVS key inventory
        "otadata":  None,
        "phy_init": None,
        "factory":  tc_fw_size,
        "ota_0":    tc_fw_size,
        "ota_1":    tc_fw_size,
        "dut_fw":   (pfw_size + HDR) if pfw_size else None,
        "prod_fw":  (prod_fw_size + HDR) if prod_fw_size else None,
        "recipes":  4096,       # 1 recipe currently loaded
    }

    descriptions: dict[str, str] = {
        "nvs":      "WiFi creds, MQTT cfg, provision keys, cal JSON, ota_root_ca (4KB PEM), bd_seq",
        "otadata":  "OTA boot selection (system)",
        "phy_init": "WiFi/BT PHY calibration (system)",
        "factory":  "Initial firmware — USB flash only",
        "ota_0":    "OTA slot A",
        "ota_1":    "OTA slot B",
        "dut_fw":   "Cached PFW test stub — SWD flash to DUT before test",
        "prod_fw":  "Cached Sensit production FW — SWD flash to DUT after tests pass",
        "recipes":  "LittleFS: JSON test recipes (~4 KB each)",
    }

    # ── Header ──
    W = 100
    print("=" * W)
    print(f"FLASH MAP — ESP32-S3 WROOM N16R8 — {total_flash // (1024*1024)} MB ({total_flash:,} bytes)")
    print("=" * W)
    print()

    # ── Bootloader ──
    bl_end = parts[0]["offset"] if parts else 0x9000
    print(f"  0x{0:06X}  ┌{'─' * 88}┐")
    print(f"           │  {'Bootloader + Partition Table':<86}│")
    print(f"           │  {bl_end // 1024} KB (0x0000–0x{bl_end - 1:04X}){' ' * 62}│")

    for i, part in enumerate(parts):
        name = part["name"]
        offset = part["offset"]
        size = part["size"]
        end = offset + size
        used = usage.get(name)
        desc = descriptions.get(name, "")

        # Utilization string
        if used is not None and size > 0:
            pct = used / size * 100
            headroom = size - used
            if pct > 80:
                flag = "⚠ "
            elif pct < 5 and size > 65536:
                flag = "◇ "
            else:
                flag = "  "
            util_str = f"{flag}{pct:5.1f}% used  — {headroom / 1024:.0f} KB free"
        else:
            util_str = "  system/fixed"

        size_str = f"{size // 1024} KB" if size < 1024 * 1024 else f"{size // (1024*1024)} MB"
        used_str = f"{used / 1024:.1f} KB" if used is not None else "—"

        print(f"  0x{offset:06X}  ├{'─' * 88}┤")
        print(f"           │  {name:<12} [{part['type']}/{part['subtype']}]"
              f"    size: {size_str:>7}    used: {used_str:>9}   {util_str:<24}│")
        print(f"           │  {desc:<86}│")

        # Check for gap before next partition
        if i < len(parts) - 1:
            next_off = parts[i + 1]["offset"]
            gap = next_off - end
            if gap > 0:
                print(f"  0x{end:06X}  │  *** GAP: {gap:,} bytes ({gap / 1024:.0f} KB) ***{' ' * 52}│")

    last_end = parts[-1]["offset"] + parts[-1]["size"]
    unalloc = total_flash - last_end
    if unalloc > 0:
        print(f"  0x{last_end:06X}  ├{'─' * 88}┤")
        print(f"           │  {'UNALLOCATED':<12}                            {unalloc:,} bytes ({unalloc / 1024:.0f} KB){' ' * 26}│")
    print(f"  0x{total_flash:06X}  └{'─' * 88}┘")

    # ── Summary ──
    print()
    print("=" * W)
    print("SUMMARY")
    print("=" * W)
    alloc = sum(p["size"] for p in parts)
    print(f"  Total flash:       {total_flash:>12,} bytes  ({total_flash // (1024*1024)} MB)")
    print(f"  Bootloader+PT:     {bl_end:>12,} bytes  ({bl_end // 1024} KB)")
    print(f"  Partitions:        {alloc:>12,} bytes  ({alloc / (1024*1024):.2f} MB)")
    print(f"  Unallocated:       {unalloc:>12,} bytes  ({unalloc // 1024} KB)")
    print()

    # ── Key binaries ──
    print("  KEY BINARIES")
    if tc_fw_size is not None:
        print(f"    TC firmware.bin:         {tc_fw_size:>10,} bytes  ({tc_fw_size / 1024:.0f} KB)")
    else:
        print(f"    TC firmware.bin:         {'(not found)':>10}")
    if pfw_size is not None:
        print(f"    DUT PFW (test stub):     {pfw_size:>10,} bytes  ({pfw_size / 1024:.1f} KB)")
    else:
        print(f"    DUT PFW (test stub):     {'(not found)':>10}")
    if prod_fw_size is not None:
        print(f"    Sensit production FW:    {prod_fw_size:>10,} bytes  ({prod_fw_size / 1024:.1f} KB)")
    else:
        print(f"    Sensit production FW:    {'(not found)':>10}")
    print()

    # ── Risk table ──
    print("  RISK ASSESSMENT")
    hdr = f"  │ {'Partition':<10} │ {'Size':>10} │ {'Used':>10} │ {'Free':>10} │ {'Util':>7} │ {'Risk':<30} │"
    sep = f"  ├{'─' * 12}┼{'─' * 12}┼{'─' * 12}┼{'─' * 12}┼{'─' * 9}┼{'─' * 32}┤"
    print(f"  ┌{'─' * 12}┬{'─' * 12}┬{'─' * 12}┬{'─' * 12}┬{'─' * 9}┬{'─' * 32}┐")
    print(hdr)
    print(sep)

    for part in parts:
        name = part["name"]
        size = part["size"]
        used = usage.get(name)
        if used is None:
            continue

        free = size - used
        pct = used / size * 100

        if name == "prod_fw" and pct > 80:
            risk = "HIGH — only {:.0f} KB headroom".format(free / 1024)
        elif name == "prod_fw":
            risk = "WATCH — {:.0f} KB headroom".format(free / 1024)
        elif name == "dut_fw" and pfw_size and pfw_size < size * 0.05:
            risk = "OVER-PROVISIONED ({:.0f} KB waste)".format(free / 1024)
        elif pct > 80:
            risk = "WATCH — {:.0f} KB headroom".format(free / 1024)
        else:
            risk = "LOW"

        print(f"  │ {name:<10} │ {size / 1024:>8.0f} KB │ {used / 1024:>8.1f} KB │ {free / 1024:>8.0f} KB │ {pct:>5.1f}% │ {risk:<30} │")

    print(f"  └{'─' * 12}┴{'─' * 12}┴{'─' * 12}┴{'─' * 12}┴{'─' * 9}┴{'─' * 32}┘")


if __name__ == "__main__":
    main()

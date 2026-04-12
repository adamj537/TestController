#!/usr/bin/env python3
"""gen_cn7_shorting_recipe.py — Generate g3-cn7-shorting-test.json.

Writes the recipe JSON to recipes/g3-cn7-shorting-test.json.

Usage:
    python3 scripts/gen_cn7_shorting_recipe.py
    python3 scripts/gen_cn7_shorting_recipe.py --version 1.0.1
"""
from __future__ import annotations
import argparse
import json
import os

# (mux, ch, pin, signal_id, conn_label, conn_num)
# SKIPPED: PB6 GPS_USART1_TX → (3,9), PB7 GPS_USART1_RX → (3,13) — CMD UART
CN7_PINS = [
    (2, 12, "PB9",  "BT_GPS_I2C1_SDA",     "CN7-5",  5),
    (2, 10, "PB8",  "BT_GPS_I2C1_SCL",     "CN7-6",  6),
    (2, 11, "PD0",  "GPS_SPI3_CS",          "CN7-7",  7),
    (2,  8, "PE8",  "BT_GPIO2",             "CN7-8",  8),
    (2,  9, "PE7",  "BT_GPIO1",             "CN7-9",  9),
    (2,  6, "PE4",  "BT_SPI3_CS",           "CN7-10", 10),
    (2,  7, "PD6",  "BT_USART2_RX",         "CN7-11", 11),
    (2,  4, "PD5",  "BT_USART2_TX",         "CN7-12", 12),
    (3,  5, "PD4",  "BT_USART2_RTS",        "CN7-13", 13),
    (2,  5, "PD3",  "BT_USART2_CTS",        "CN7-14", 14),
    (3,  7, "PC12", "BT_GPS_SPI3_MOSI",     "CN7-16", 16),
    (3, 11, "PC10", "SPI3_SCK_GPS_GPIO1",   "CN7-18", 18),
    (3, 14, "PB5",  "GPS_GPIO2",            "CN7-19", 19),
    (3, 15, "PC11", "BT_GPS_SPI3_MISO",     "CN7-20", 20),
]


def build_steps() -> list[dict]:
    steps: list[dict] = []

    # ── Bring-up ──────────────────────────────────────────────────────────────
    steps.append({
        "id": "i2c", "label": "TCC I2C Bus & Rail Check",
        "enabled": True, "onError": "abort",
        "primitive": "i2c", "criticality": "CRITICAL",
    })
    steps.append({
        "id": "dut_program", "label": "Program PFW via SWD",
        "params": {"target": "pfw", "verify": True, "timeout_s": 60},
        "enabled": True, "onError": "abort",
        "primitive": "dut_program", "criticality": "CRITICAL",
    })
    steps.append({
        "id": "power_check", "label": "DUT Power Delivery (3.3V)",
        "params": {"v_nominal_mv": 3300, "v_tolerance_pct": 5,
                   "i_min_ma": 2, "i_max_ma": 250},
        "enabled": True, "onError": "skip",
        "primitive": "power_check", "criticality": "REQUIRED",
    })
    steps.append({
        "id": "dut_heartbeat", "label": "DUT Heartbeat (PFW running)",
        "enabled": True, "onError": "abort",
        "primitive": "dut_heartbeat", "criticality": "CRITICAL",
    })
    steps.append({
        "id": "dut_enter_test", "label": "DUT Enter Test Mode",
        "enabled": True, "onError": "abort",
        "primitive": "dut_enter_test", "criticality": "CRITICAL",
    })

    # ── Init: drive all CN7 pins to push-pull OUTPUT LOW ─────────────────────
    # Eliminates pull-up artifacts on I2C pins (PB9/PB8) and any other
    # pins that default HIGH before test mode configures them.
    for mux, ch, pin, sig, conn, num in CN7_PINS:
        steps.append({
            "id":          f"cn7_init_{pin.lower()}",
            "label":       f"Init {conn} {pin} → OUTPUT LOW",
            "params":      {"pin": pin},
            "enabled":     True,
            "onError":     "skip",
            "primitive":   "dut_gpio_clear",
            "criticality": "REQUIRED",
        })

    # ── Sweep ─────────────────────────────────────────────────────────────────
    for d_mux, d_ch, d_pin, d_sig, d_conn, d_num in CN7_PINS:
        # Drive HIGH
        steps.append({
            "id":          f"cn7_{d_num}_set_h",
            "label":       f"Drive {d_conn} {d_sig}/{d_pin} HIGH",
            "params":      {"pin": d_pin, "level": "HIGH"},
            "enabled":     True,
            "onError":     "skip",
            "primitive":   "dut_gpio_set",
            "criticality": "REQUIRED",
        })
        # Self verify HIGH
        steps.append({
            "id":          f"cn7_{d_num}_self_h",
            "label":       f"{d_conn} {d_pin} HIGH — self verify",
            "params":      {"mux": d_mux, "ch": d_ch,
                            "min_mv": 2700, "max_mv": 3600,
                            "check_id": f"cn7_{d_num}_self_h"},
            "enabled":     True,
            "onError":     "skip",
            "primitive":   "mux_read",
            "criticality": "REQUIRED",
        })
        # Neighbor no-short checks
        for n_mux, n_ch, n_pin, n_sig, n_conn, n_num in CN7_PINS:
            if n_num == d_num:
                continue
            steps.append({
                "id":          f"cn7_{d_num}_nbr_{n_num}",
                "label":       f"{n_conn} {n_pin} no short — {d_pin}=H",
                "params":      {"mux": n_mux, "ch": n_ch,
                                "min_mv": 0, "max_mv": 500,
                                "check_id": f"cn7_{d_num}_nbr_{n_num}"},
                "enabled":     True,
                "onError":     "skip",
                "primitive":   "mux_read",
                "criticality": "REQUIRED",
            })
        # Drive LOW
        steps.append({
            "id":          f"cn7_{d_num}_clr",
            "label":       f"Drive {d_conn} {d_sig}/{d_pin} LOW",
            "params":      {"pin": d_pin},
            "enabled":     True,
            "onError":     "skip",
            "primitive":   "dut_gpio_clear",
            "criticality": "REQUIRED",
        })
        # Self verify LOW
        steps.append({
            "id":          f"cn7_{d_num}_self_l",
            "label":       f"{d_conn} {d_pin} LOW — self verify",
            "params":      {"mux": d_mux, "ch": d_ch,
                            "min_mv": 0, "max_mv": 300,
                            "check_id": f"cn7_{d_num}_self_l"},
            "enabled":     True,
            "onError":     "skip",
            "primitive":   "mux_read",
            "criticality": "REQUIRED",
        })

    return steps


def main() -> None:
    ap = argparse.ArgumentParser(description="Generate g3-cn7-shorting-test.json")
    ap.add_argument("--version", default="1.0.0", help="Recipe version string")
    args = ap.parse_args()

    steps = build_steps()
    recipe = {
        "schemaVersion": 1,
        "recipeId":      "g3-cn7-shorting-test",
        "recipeVersion": args.version,
        "name":          "G3-CN7-Shorting-Test",
        "timeoutMs":     300000,
        "steps":         steps,
    }

    out_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "recipes", "g3-cn7-shorting-test.json",
    )
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(recipe, f, indent=2)
        f.write("\n")

    print(f"Written: {out_path}  ({len(steps)} steps, v{args.version})")


if __name__ == "__main__":
    main()

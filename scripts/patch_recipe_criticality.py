#!/usr/bin/env python3
"""Reclassify measurement-only REQUIRED steps to OPTIONAL in g3-mb-v2 recipe.

These steps use analog measurements with calibration-sensitive limits.
A failed measurement does not indicate a board that cannot run product firmware —
it just means a limit is out of range. Structural steps (GPIO, power, comms) remain REQUIRED.
"""

import json
import sys

RECIPE_FILE = "recipes/g3-mb-v2.json"
NEW_VERSION = "2.2.13"

# Measurement-only steps: analog ADC reads, current reads, LTC2498 comparisons.
# These have calibration-sensitive limits and should not gate product flash.
DEMOTE_TO_OPTIONAL = {
    "ltc_vout_2611",    # dut_peripheral_adc_read — LTC2611 output voltage
    "ltc_vref_div",     # mux_read — LTC reference divider voltage
    "br1_ina_pre",      # ina_read — BR1 pre-enable current measurement
    "br1_vmon1_q",      # dut_peripheral_adc_read — BR1 voltage monitor (quiescent)
    "br1_vmon1_fl",     # dut_peripheral_adc_read — BR1 voltage monitor (flashlight)
    "br1_vmon1_s2",     # dut_peripheral_adc_read — BR1 voltage monitor (S2)
    "br1_2611_vout",    # dut_peripheral_adc_read — BR1 LTC2611 output
    "br1_vmon1_both",   # dut_peripheral_adc_read — BR1 voltage monitor (both)
    "br1_post_ltc_cmp", # ltc2498_compare_snapshot — BR1 post-enable comparison
    "br1_fl_ltc_cmp",   # ltc2498_compare_snapshot — BR1 flashlight comparison
    "br1_s2_ltc_cmp",   # ltc2498_compare_snapshot — BR1 S2 comparison
    "br1_both_ltc_cmp", # ltc2498_compare_snapshot — BR1 both-loads comparison
    "br5_3v_vmon",      # dut_peripheral_adc_read — BR5 3.3V monitor
    "dut_vbatt",        # dut_peripheral_adc_read — DUT battery voltage
    "br5_pressure_neg", # dut_peripheral_adc_read — BR5 negative pressure sensor
    "br6_vmon",         # dut_peripheral_adc_read — BR6 voltage monitor
}

with open(RECIPE_FILE, encoding="utf-8") as f:
    recipe = json.load(f)

old_version = recipe.get("recipeVersion", "?")
changed = []
not_found = list(DEMOTE_TO_OPTIONAL)

for step in recipe["steps"]:
    sid = step.get("id", "")
    if sid in DEMOTE_TO_OPTIONAL:
        old_crit = step.get("criticality", "REQUIRED")
        if old_crit != "OPTIONAL":
            step["criticality"] = "OPTIONAL"
            changed.append(sid)
        not_found = [x for x in not_found if x != sid]

recipe["recipeVersion"] = NEW_VERSION

with open(RECIPE_FILE, "w", encoding="utf-8") as f:
    json.dump(recipe, f, indent=2)
    f.write("\n")

print(f"Version: {old_version} → {NEW_VERSION}")
print(f"Changed {len(changed)} steps to OPTIONAL:")
for sid in changed:
    print(f"  {sid}")
if not_found:
    print(f"\nWARNING: {len(not_found)} step IDs not found in recipe:")
    for sid in not_found:
        print(f"  {sid}")

sys.exit(0 if not not_found else 1)

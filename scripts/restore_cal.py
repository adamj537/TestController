#!/usr/bin/env python3
"""Restore VDUT calibration values via cal_set DCMD.

Usage:
    python3 scripts/restore_cal.py
"""
import json
import sys
import time

import paho.mqtt.client as mqtt

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from tc_config import BROKER, TC_SERIAL as NODE

PORT = 1883
GROUP = "SensitMfg"
CHANNEL = 0

DCMD_TOPIC = f"spBv1.0/{GROUP}/DCMD/{NODE}/CH{CHANNEL}"

# Cal values from UI cache (2026-03-23T17:21:53Z)
CAL_PARAMS: list[tuple[str, int]] = [
    ("vdut1.intercept", 12016),
    ("vdut1.slope",     -100),
    ("vdut2.intercept", 11986),
    ("vdut2.slope",     -99),
]


def main() -> int:
    client = mqtt.Client()
    client.connect(BROKER, PORT, 10)
    client.loop_start()
    time.sleep(1)

    for key, value in CAL_PARAMS:
        payload = json.dumps({"cmd": "cal_set", "key": key, "value": str(value)})
        client.publish(DCMD_TOPIC, payload)
        print(f"  [TX] cal_set  {key} = {value}")
        time.sleep(0.5)

    client.loop_stop()
    client.disconnect()
    print("\nDone — cal_set saves automatically on each write.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

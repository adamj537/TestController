# TC Firmware — Operator Notes

<!--
  Transcluded into: docs/operator/ sections
  Last updated: 2026-03-22 (console moved to USB-Serial-JTAG, recipe v2.0)
-->

The TC (Test Controller) firmware runs on the ESP32-S3 inside the fixture.
Operators interact with it indirectly through the HMI display and LED
indicators. This page documents what operators see and when to escalate.

## Healthy Boot Sequence

When the fixture is powered on, the TC boots in approximately 3 seconds:

1. **LED off** — bootloader running
2. **Green LED blinking** — firmware starting, connecting to WiFi and MQTT
3. **Green LED solid** — "READY" / "INSERT DUT" displayed on HMI

If the TC connects to the MQTT broker, the HMI shows **INSERT DUT**.
If the broker is unreachable, the HMI shows **STANDALONE OK** — the fixture
can still run tests, but results are stored locally and not reported.

## HMI Display States

The 16×2 LCD shows the current fixture state:

| Line 1 | Line 2 | LED State | Meaning |
|--------|--------|-----------|---------|
| READY | INSERT DUT | Green solid | Idle, broker connected — insert a DUT to begin |
| READY | STANDALONE OK | Green solid | Idle, no broker — tests run locally |
| CHECKING... | *(step name)* | Green blink | Pre-check selftest running after DUT insertion |
| PRE-TEST... | *(step name)* | Green blink | Pre-test gate (SWD program, power check) |
| FLASHING DUT... | *(step name)* | Green blink | Programming DUT firmware via SWD |
| TESTING... | *(step name)* | Green blink | Recipe test in progress |
| \*\*\*\* PASS \*\*\*\* | DUT: XXXX-XXXX | Green solid (3s) | Test passed — remove DUT |
| \*\*\*\* FAIL \*\*\*\* | *(failing step)* | Red solid (5s) | Test failed — remove DUT, see step ID |
| SELFTEST FAIL | CHECK FIXTURE | Red solid | Fixture hardware selftest failed |
| FLASH FAIL | CHECK DUT PWR | Red solid | SWD programming failed |
| ABORTED | REMOVE DUT | Red blink | Test was aborted (critical step failed) |
| NO BROKER | STANDALONE OK | Green solid | MQTT broker unreachable (not an error) |
| \*\* E-STOP \*\* | RESET TO CLEAR | Red solid | Emergency stop — reset fixture to clear |

## Normal Test Cycle

1. Insert DUT into the fixture (pogos make contact)
2. TC detects DUT automatically (~1 second)
3. HMI shows **CHECKING...** — quick selftest of fixture hardware
4. If selftest passes, full recipe runs (**TESTING...**)
5. HMI shows **PASS** or **FAIL** with the DUT reference or failing step
6. Remove DUT — fixture returns to **READY**

No buttons need to be pressed. The test starts automatically on DUT insertion.

## When to Escalate

| Symptom | Action |
|---------|--------|
| HMI blank, no LED | Check fixture power supply. If powered, escalate to maintenance. |
| **SELFTEST FAIL** / CHECK FIXTURE | Do not insert DUT. Escalate to maintenance — fixture hardware issue. |
| **FLASH FAIL** persists across multiple DUTs | Escalate — likely SWD pogo or fixture wiring issue. |
| Green LED blink but no HMI text | LCD cable may be loose. Escalate to maintenance. |
| **NO BROKER** when broker should be online | Check network cable / WiFi. Not blocking — tests still run. |
| **E-STOP** | Resolve the E-STOP condition, then power-cycle the fixture. |
| Repeated FAIL on known-good DUTs | Escalate — fixture calibration or pogo contact issue. |

## Display Hold Times

- **PASS**: displayed for 3 seconds, then returns to READY
- **FAIL**: displayed for 5 seconds (operator reads failing step ID)
- **ABORTED / E-STOP / SELFTEST FAIL**: held until operator action (DUT removal or fixture reset)

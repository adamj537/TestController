# TC Firmware — Developer Notes

<!--
  Transcluded into: docs/developer/firmware/tc-firmware.md
-->

## Architecture

TC firmware uses a **hybrid pattern**:

- `embedded/tester-client/` — G3-specific code (recipes, DUT interface, bringup shell)
- `embedded/tester-client/common/` — `tc-firmware-common` submodule (HAL, BSP, shared utilities)

## Build environments

| Environment | Target | Use |
|---|---|---|
| `esp32-Devkit` | ESP32-S3 hardware | Production build, OTA |
| `native` | Linux/WSL | Unit testing |

```bash
pio run -e esp32-Devkit          # build for hardware
pio test -e native               # run unit tests (76 passing as of FW-1)
```

## FW-1 status

Phases 1–5 complete.  PR #12 open on `INTenX/G3-MB-Embedded-Tester-Client`.
Hardware bringup complete (TCP console, selftest, vdac characterization).

## Bringup shell commands

The bringup shell is an ESP console REPL, exposed on UART and TCP port 4242.

### selftest

```
selftest [all|i2c|adc|wifi|ota|temp|vdut|mux|heartbeat|char]
```

Runs onboard health checks.  Without a subcommand, `all` is assumed.
`selftest all` produces **20 pass/fail results** and takes ~8 s
(dominated by the VDUT 3-point sweep).

| Group | What it tests |
|---|---|
| `i2c` | Bus init, INA219 × 2 probe, ADC128D818 probe, 3.3 V rail, 5 V rail, temp sensor |
| `adc` | Internal ESP32 ADC on GPIO1 |
| `wifi` | WiFi STA connection and RSSI |
| `ota` | Running partition label and OTA image state |
| `temp` | ADC128D818 CH7 internal temperature diode (°C, ±2°C per SNAS483F Table 16 Eq 2/3) |
| `vdut` | 3-point VDUT1/VDUT2 sweep — see below |
| `mux` | TIE analog mux scan — all 4 × 16 channels via ADC128D818 CH0–CH3 |
| `heartbeat` | DUT PA9 1 Hz toggle → TIE MUX U12 Ch2 → ADC128 CH2, sampled 2.5 s |
| `char` | Full 0–100 % characterization sweep — see below (not in `all`, takes ~33 s) |

The I²C group uses a **two-pass probe** to work around the TCC v1 SDA/SCL swap
errata (see TCC board developer notes):

1. Default orientation (SDA=15, SCL=16) — probe INA219s at 0x40 and 0x41
2. Swapped orientation (SDA=16, SCL=15) — probe ADC128D818 at 0x1D, read CH4/CH5, read CH7 temp
3. Restore default orientation

#### selftest vdut — 3-point VDUT sweep

Exercises both VDUT regulators with a 3-point duty cycle sweep.  INA219 V_BUS
is the primary (and only) voltage measurement — ADC128 CH6/CH7 are no longer
read here; those channels are reserved for DUT_VIN and ISO_POWER_IN monitoring
on the next board spin.

**Sequence per measurement point:**

1. Set duty on both channels (LEDC + IOMUX re-route)
2. Wait `VDAC_STEP_SETTLE_MS` (1500 ms) for regulator to settle
3. Read INA219 #0/#1: V_BUS, current, power (normal bus throughout — no swap)

**Pass criteria:**

| Criterion | Detail |
|---|---|
| Absolute range | INA219 V_BUS within per-point window (see table) |
| Monotonic | V(10%) > V(50%) > V(90%) — confirms regulator responds to PWM |

| Duty | V_BUS window | Typical value (no load) |
|---|---|---|
| 10 % | 8000–12000 mV | ~9940 mV |
| 50 % | 5000–9000 mV | ~7010 mV |
| 90 % | 1000–5000 mV | ~3060 mV |

**Why INA219 V_BUS over ADC128:** INA219 V_BUS accuracy is ±0.5% max at room
temp, ±1% over −25–85 °C (SBOS448G Table 7.5).  The ADC128 CH6/CH7 divider
used 5%-tolerance resistors (C15401, loaded vs 1% design intent), producing a
~±4% error floor that cannot be recovered without a board respin.  INA219 V_BUS
is also the more meaningful measurement — it reads at IN−, the DUT connector
side of the shunt (0.1 mV drop at 1 mA, negligible).

**INA219 configuration:** PGA=/1 (±40 mV FSR, config `0x219F`, CAL `0xA000` =
40960) → `Current_LSB = 10 µA`, `Power_LSB = 200 µW`.

**API promoted for selftest use** (from `cmd_vdac.h`):

- `vdac_set_duty(ch_idx, duty_pct)` — set LEDC duty; re-asserts IOMUX
- `vdac_set_enable(ch_idx, enable)` — drive ENA GPIO
- `adc128_ensure_running()` — arm Mode-1 conversions (call after every bus reinit)
- `adc128_read_mon(ch, &mv)` — read CH6/CH7 with 4× divider applied
- `i2c_write_reg16(addr, reg, val)` — 3-byte big-endian write for INA219 CAL/config

#### selftest char — full characterization sweep

Sweeps both VDUT regulators 0–100 % in 5 % steps (~33 s total) and prints a
table of ADC128D818 CH6/CH7 voltages plus all four INA219 data registers per
point.  Not included in `selftest all`.

**Output columns:**

| Column | Source | Units |
|---|---|---|
| duty | LEDC setpoint | % |
| VBus0 / VBus1 | INA219 #0/#1 register 0x02 (BD >> 3 × 4) | mV |
| Vsht0 / Vsht1 | INA219 #0/#1 register 0x01 × 10 | µV |
| I0 / I1 | INA219 #0/#1 current register × 10 µA | µA |
| P0 / P1 | INA219 #0/#1 power register × 200 µW | µW |

ADC128 CH6/CH7 (formerly VDUT1Mon/VDUT2Mon) are no longer read here — reserved
for DUT_VIN and ISO_POWER_IN on the next board spin.

**Typical no-load output (excerpt):**

```
duty  VBus0  Vsht0   I0      P0     VBus1  Vsht1   I1      P1
  %    mV     µV      µA     µW      mV     µV      µA     µW
   0  10764    +110   +1100   11800  10692    +100   +1000   10800
  50   7032     +70    +600    5000   7000     +60    +600    4200
 100   2152      +0      +0       0   2228      +0      +0       0
```

### vdac — VDUT regulator DAC control

Controls the two DUT supply regulators (VDUT1 via U1, VDUT2 via U7) using LEDC
PWM → RC filter → MP2315SGJ-Z feedback injection.

```
vdac char                         # sweep 0–100 % and print duty-vs-voltage table
vdac set <1|2|both> <duty_pct>   # enable and set duty cycle (0–100)
vdac off                          # disable both channels
```

**Characterization sweep** (`vdac char`):

- Initializes both channels at 0 % duty
- Enables both regulators
- Switches to swapped I²C bus for ADC128D818
- Waits 2 s for regulator to settle from initial state
- Steps 0 % → 100 % in 5 % increments; waits 1500 ms per step for regulator
  control loop to settle
- Reads VDUT1Mon (CH6) and VDUT2Mon (CH7) from ADC128D818 at each step
- Disables both channels and restores I²C bus on completion

**Set command** (`vdac set`):

- Asserts ENA GPIO for selected channel(s)
- Calls `ledc_channel_config()` to set duty and re-assert IOMUX

#### GPIO 19/20 USB IOMUX — design constraint

GPIO 19 and 20 are the ESP32-S3 USB D-/D+ pads.  The USB peripheral retains
IOMUX priority.  `ledc_set_duty()` + `ledc_update_duty()` do **not** drive
these pads after a USB reset.  The workaround is to call `ledc_channel_config()`
on every duty update, which re-routes IOMUX to LEDC on each call.  The
"GPIO not usable" log message is non-fatal.

```c
/* Correct: re-assert IOMUX on every duty change */
ledc_channel_config(&ch_cfg);   /* sets duty AND re-routes IOMUX */

/* Wrong: IOMUX not re-asserted, GPIO19/20 may not drive */
ledc_set_duty(mode, ch, duty);
ledc_update_duty(mode, ch);
```

#### Regulator settling time

The RC filter (1 kΩ, 0.1 µF, τ = 100 µs) settles in < 1 ms.  The actual
bottleneck is the MP2315SGJ-Z control loop combined with the 47 µF output
capacitor: measured settling time for a step change is **~750 ms**.  The
firmware uses:

- `VDAC_STEP_SETTLE_MS = 1500 ms` (sweep steps)
- `VDAC_INIT_SETTLE_MS = 2000 ms` (initial enable)

### i2c — I²C bus management

```
i2c scan                          # scan for devices on current bus
i2c probe <addr>                  # probe single address
i2c read <addr> <reg> <len>       # read register(s)
i2c write <addr> <reg> <bytes…>   # write register
i2c reinit <sda_gpio> <scl_gpio>  # reinitialize bus with new pin assignment
```

Use `i2c reinit 16 15` to reach the ADC128D818 (swapped bus).
Use `i2c reinit 15 16` to restore the default orientation for INA219s.

### gpio, pwm, adc, wifi, ota

See `help` in the bringup shell for usage of the remaining command groups.

## How to add a new bringup command

1. Create `src/cmd_<name>.c` and `src/cmd_<name>.h`
2. Implement a static `do_<name>()` dispatcher and `register_<name>_commands()`
3. Add `#include "cmd_<name>.h"` and `register_<name>_commands()` to `src/main.cpp`
4. Add `src/cmd_<name>.c` to `CMakeLists.txt` (or PlatformIO will pick it up automatically)

Follow the pattern in `cmd_vdac.c` for commands that touch hardware peripherals.

## State machine

The test cycle is driven by a state machine in `tc_statemachine.c`, controlled
via the `sm` shell command or the `start` DCMD over MQTT.

### States

```
Idle → Precheck → Testing → Pass/Fail → Idle
```

| State | Description |
|---|---|
| **Idle** | Waiting for `sm start` or DCMD `start` |
| **Precheck** | Runs `selftest quick` (8 checks: I²C, rails, WiFi) |
| **Testing** | Runs `selftest fixture` (18 checks: full VDUT sweep + I²C + WiFi + OTA) |
| **Pass** | All fixture checks passed — holds 3 s for operator visibility |
| **Fail** | One or more checks failed — holds 3 s, publishes failing check ID |

### Sequence

1. `sm start` → transition to **Precheck**, run quick selftest
2. Quick selftest passes → read DUT UID96 via SWD (`identify_dut()`) → transition to **Testing**, run fixture selftest
3. Fixture selftest completes → transition to **Pass** or **Fail**
4. Publish `type=result` DDATA with outcome, duration, DUT serial, failing check ID
5. Hold 3 s → transition back to **Idle**

### MQTT integration

Each state transition publishes a `type=state` DDATA.  Selftest results publish
as `type=selftest` DDATA.  The final result publishes as `type=result` DDATA
(QoS 1) containing:

- `outcome`: `"pass"` or `"fail"`
- `failed_step`: check ID of first failure (null on pass)
- `duration_ms`: total cycle time from start
- `dut_serial_full`: STM32 UID96 (24-char hex, read via SWD)
- `dut_serial_ref`: CRC32-derived short reference (XXXX-XXXX)
- `recipe_id` / `recipe_version`: test recipe identification

### Shell commands

```
sm status    # print current state
sm start     # begin test cycle (must be in Idle)
sm abort     # abort and return to Idle
```

## PSRAM configuration

The Freenove ESP32-S3 WROOM N16R8 has 8 MB PSRAM (APS6408L) embedded in the
module.  PSRAM is enabled (`CONFIG_SPIRAM=y`) in **Quad SPI mode** and uses
**deferred init** — the `SPIRAM_BOOT_HW_INIT` option is disabled because early
boot PSRAM init causes `RTCWDT_RTC_RST` on this board (investigated 2026-03-11).

Instead, `esp_psram_init()` and `esp_psram_extram_add_to_heap_allocator()` are
called from `app_main()`.  After init, ~8 MB of PSRAM is available via
`heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.

!!! note "HW-010: Resolved — SWCLK moved to GPIO 45, OPI PSRAM restored"
    SWCLK was previously on GPIO 37 (SPIDQS), which conflicted with OPI PSRAM
    when both were active.  SWCLK is now on **GPIO 45** — a free strapping pin
    that the STM32's internal SWCLK pull-down holds LOW during ESP32-S3 reset,
    eliminating the strapping risk.  OPI (Octal) PSRAM is active again.

**OTA limitation:** PSRAM-layout builds move the DROM segment to `0x3c0d0020`,
which causes OTA image verification to crash (`Interrupt wdt timeout on CPU1`)
due to MMU mapping conflicts.  Use UART flash (`pio run -t upload --upload-port
/dev/ttyACM0`) instead — this preserves NVS (WiFi credentials).

| Setting | Value |
|---|---|
| `CONFIG_SPIRAM` | `y` |
| `CONFIG_SPIRAM_MODE_OCT` | `y` |
| `CONFIG_SPIRAM_SPEED_40M` | `y` |
| `CONFIG_SPIRAM_BOOT_HW_INIT` | `n` (deferred to app_main) |
| `CONFIG_SPIRAM_USE_CAPS_ALLOC` | `y` |
| `CONFIG_ESPTOOLPY_FLASHMODE` | `dio` |
| `CONFIG_ESPTOOLPY_FLASHSIZE` | `8MB` |

## WDT configuration

WDT is configured via `sdkconfig.esp32-Devkit` (Kconfig), not build flags:

| Timer | Setting | Value |
|---|---|---|
| Task WDT | `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 5 s |
| Task WDT panic | `CONFIG_ESP_TASK_WDT_PANIC` | disabled (logs only) |
| Int WDT | `CONFIG_ESP_INT_WDT_TIMEOUT_MS` | 300 ms |

Long-running commands (`selftest all`, `vdac char`) take up to ~10 s and are
protected against the task WDT because they run via `xTaskCreate` — each
spawned task feeds its own idle tick.  Shell commands that block on the console
task may still log a WDT warning if they exceed 5 s; this is non-fatal.

## SWD bit-bang — DUT firmware flashing

The TC can program DUT firmware over SWD without a JTAG probe.  GPIO45 drives
SWCLK and GPIO38 drives SWDIO, routed TCC → TIE J15 → DUT CN6.

### Commands

```
swd flash <url> [nopwrcycle] [--store]  # download, flash DUT, optionally cache
swd flash local [nopwrcycle]            # flash DUT from cached dut_fw partition
swd probe [half_us] [nojtagtoswd]       # diagnostic: print IDCODE, CTRL/STAT, ACK
swd cs                                  # power-up DP + AHB-AP and print CTRL/STAT fields
```

### Typical flash workflow

#### First board (network available)

```bash
# 1. Serve the DUT binary from WSL
cd ~/G3-MB-Tester/embedded/dut-firmware
python3 -m http.server 8080 --directory .pio/build/g3-dut/ &

# 2. On the TC TCP console (10.0.0.244:4242) — flash AND cache the image
swd flash http://10.0.0.246:8080/firmware.bin --store
# Expected: [PASS] swd flash complete <N> bytes at 0x08000000
# Also: "Stored N bytes to dut_fw partition"

kill $(lsof -ti:8080)   # clean up server
```

#### Subsequent boards (no network needed)

```
swd flash local
# Loads image from dut_fw partition — no HTTP required
```

#### Remote update via MQTT DCMD

```json
{"cmd": "ota", "url": "http://host/firmware.bin", "target": "dut_firmware"}
```

Stores the binary in the `dut_fw` partition without flashing the DUT or rebooting
the TC.  Once stored, use `swd flash local` to program individual boards.

To update TC firmware instead (default when `target` is absent):

```json
{"cmd": "ota", "url": "http://host/tc-firmware.bin"}
```

### `swd flash` sequence

1. Download binary from URL into heap buffer
2. Power-cycle DUT (de-assert / re-assert VDUT)
3. Send JTAG-to-SWD switching sequence (0xFF × 8, 0x9E 0xE7, 0xFF × 8, idle 4)
4. Read DP IDCODE — confirms target is alive (STM32L476: `0x2BA01477`)
5. Power-up DP: write CTRL/STAT `0x50000F00` (CDBGPWRUPREQ | CSYSPWRUPREQ |
   MASKLANE=0xF), poll until CDBGPWRUPACK | CSYSPWRUPACK both asserted
6. Init AHB-AP: write CSW `0xA2000052` (DbgSwEnable | MasterDebug | HPROT |
   AddrInc=single | Size=word)
7. Halt CPU via DHCSR write (`0xA05F0003`)
8. Unlock STM32L476 flash: write KEYR `0x45670123` then `0xCDEF89AB`
9. Erase pages: write PECR PER+PNB for each 2 kB page, poll BSY
10. Program: write words via AHB-AP, poll EOP after each page
11. Lock flash, resume CPU

### 38-clock write framing (key implementation detail)

ARM ADIv5 SWD requires **38 clocks** in the write data phase, not 37.  The
off-by-one error causes the 32-bit write data to arrive at the target shifted
1 bit to the right — silently, because parity can still match by coincidence
(`WDATAERR=0`) but the register value is wrong.

The correct write frame after the 8-bit request byte:
1. **TRN** (1 clock, host→target): host tristates, clock rises — target begins
   driving ACK here.  This bit is **discarded** (not read).
2. **ACK** (3 clocks): read 3 bits from target.  Because ACK[0] was consumed
   by the TRN clock, the raw value is `{ACK[1], ACK[2], pull-up}`.
   Correction: raw 4 → OK (1), raw 5 → WAIT (2), raw 6 → FAULT (4).
3. **TRN** (1 clock, target→host): host takes back SWDIO.
4. **WDATA** (32 clocks) + **WPARITY** (1 clock): host writes data MSB→LSB.
5. **idle** (≥ 8 clocks): line idle before next request.

The read path (`swd_dp_read`) is unaffected — 37 clocks works there because
the target drives ACK starting at TRN (consistent with the read frame).

### Target notes — STM32L476

| Item | Value |
|---|---|
| IDCODE | `0x2BA01477` |
| TARGETID (DP bank 2) | `0x00000043` |
| Flash base | `0x08000000` |
| Flash page size | 2 kB |
| KEYR unlock sequence | `0x45670123`, `0xCDEF89AB` |
| DHCSR address | `0xE000EDF0` |
| Halt value | `0xA05F0003` (DBGKEY | C_DEBUGEN | C_HALT) |

---

## Calibration (`tc_cal`)

All measurement channels use a linear correction model:

```
corrected = gain × raw + offset
```

VDUT uses an inverting regulator model:

```
V_mv = slope_mv_per_pct × duty_pct + intercept_mv
duty_pct = (target_mv − intercept_mv) / slope_mv_per_pct
```

Calibration data is stored in NVS (namespace `cal`, key `cal`, JSON blob).  It
survives OTA updates.  Factory defaults are `gain=1.0`, `offset=0` (passthrough)
for all channels; VDUT `slope=0` is the **uncalibrated sentinel** — the TC
refuses to enable VDUT until a calibration run sets a non-zero slope.

### Channels

| Channel | Quantity | Keys |
|---|---|---|
| VDUT PWM DAC | Duty → voltage | `vdut.slope`, `vdut.intercept` |
| INA219 ch0 (VDUT1) | Bus voltage | `ina0.v.gain`, `ina0.v.offset_mv` |
| INA219 ch0 (VDUT1) | Current | `ina0.i.gain`, `ina0.i.offset_ma` |
| INA219 ch1 (VDUT2) | Bus voltage | `ina1.v.gain`, `ina1.v.offset_mv` |
| INA219 ch1 (VDUT2) | Current | `ina1.i.gain`, `ina1.i.offset_ma` |
| ADC128D818 ch0–7 | Rail voltages | `adc.N.gain`, `adc.N.offset_mv` |

### Console commands

```
cal show                          # print all coefficients
cal load                          # reload from NVS
cal save                          # persist in-RAM cal to NVS
cal reset                         # reset to factory defaults (does not save)
cal vdut <slope> <intercept>      # set VDUT coefficients and save
cal vdut-duty <mv>                # compute duty for target mV (dry run)
cal ina <0|1> v <gain> <offset>   # set INA219 voltage coefficients
cal ina <0|1> i <gain> <offset>   # set INA219 current coefficients
cal adc <0-7> <gain> <offset>     # set ADC128D818 channel coefficients
```

### VDUT calibration workflow

The `vdac char` command performs the hardware sweep and writes calibration:

```
vdac char    # sweeps 0–100 % duty, reads ADC128 at each step,
             # fits linear model, writes slope+intercept to tc_cal, saves
```

After `vdac char` the fixture is calibrated and `cal show` confirms the values.
No manual `cal save` is needed — `vdac char` saves automatically.

### MQTT interface

Calibration is readable/writable over MQTT by an `engineer`-role session:

| DCMD | Payload | Effect |
|---|---|---|
| `cal_get` | *(none)* | TC publishes DDATA with all `Cal/*` metrics |
| `cal_set` | `{"key":"vdut.slope","value":-87}` | Write one coefficient, persist immediately |

See the [MQTT contract](../design/architecture/mqtt-contract.md) for the full
payload schema and all supported `cal_set` keys.

`NBIRTH Properties/CalProfileVersion` is `"set"` when VDUT is calibrated
(slope ≠ 0), `"none"` otherwise.

### NVS storage details

- **Namespace**: `cal`
- **Key**: `cal` (single JSON blob; atomic write)
- **Domain**: `STORAGE_DOMAIN_CALIBRATION` → routed to NVS in `storage_spiffs.c`
- **Blob format**: JSON object with sub-objects `vdut`, `ina219`, `adc128`

The blob survives OTA because NVS is a separate flash partition from the
firmware image.

---

## Production recipe — g3-mb-v1

The production test recipe is defined in `recipes/g3-mb-v1.json`.  It runs
when the state machine starts and loads the recipe from LittleFS by ID
(`g3-mb-v1`).  Steps execute in order; a CRITICAL failure aborts immediately
and (where applicable) cuts VDUT power.

### Step summary

| # | Step ID | Criticality | Enabled | Description |
|---|---------|-------------|---------|-------------|
| 1 | `i2c` | CRITICAL | ✅ | I²C bus scan + rail check |
| 2 | `adc` | OPTIONAL | ✅ | ESP32 internal ADC sanity |
| 3 | `wifi` | REQUIRED | ✅ | WiFi connected |
| 4 | `ota` | REQUIRED | ✅ | OTA partition valid |
| 5 | `dut_read_id` | REQUIRED | ✅ | DUT STM32 UID96 via UART |
| 6 | `dut_program` | CRITICAL | ❌ | Flash PFW via SWD — disabled until PFW binary in NVS |
| 7 | `power_check` | CRITICAL | ✅ | DUT 3.3 V ± 5 %, 5–250 mA (INA219) |
| 8 | `dut_heartbeat` | CRITICAL | ❌ | PA9 1 Hz toggle via ADC128 — disabled until PFW running |
| 9 | `dut_enter_test` | CRITICAL | ✅ | UART: `ENTER_TEST` → `OK TEST_MODE_ACTIVE` |
| 10 | `dut_version` | REQUIRED | ✅ | UART: `VERSION` → `OK <fw-string>` |
| 11 | `dut_hw_rev` | REQUIRED | ✅ | UART: `HW_REV` → `OK <rev-string>` |
| 12 | `dut_uc_adc_vref` | REQUIRED | ✅ | UART: `UC_ADC_READ VREF` → 2400–2600 mV |
| 13 | `dut_uc_adc_3v_rail` | REQUIRED | ✅ | UART: `UC_ADC_READ 3V_RAIL` → 2850–3150 mV |
| 14 | `dut_flash_test` | REQUIRED | ✅ | UART: `FLASH_TEST` → response contains `PASS` |
| 15 | `dut_rtc_read` | REQUIRED | ✅ | UART: `RTC_READ` → 1550–3600 mV (coin cell) |
| 16 | `dut_exit_test` | REQUIRED | ✅ | UART: `EXIT_TEST` → `OK` |
| 17 | `mux_scan` | REQUIRED | ✅ | TIE MUX scan (4 mux × 16 ch) |

### DUT UART interface

Steps 9–16 communicate with the DUT over UART_NUM_1 (TC GPIO43 TX → DUT RX,
GPIO44 RX ← DUT TX) at 115200 8N1.  All commands follow the protocol:

```
TC → DUT:  CMD [ARGS]\r\n
DUT → TC:  OK [DATA]\r\n   (or ERR [CODE]\r\n on failure)
```

The UART driver is opened once in `dut_enter_test` and closed in
`dut_exit_test`.  If `ENTER_TEST` fails, the driver closes immediately and all
subsequent UART steps skip via the `s_uart_open` gate — no spurious UART
errors propagate.

**Blocker:** Steps 9–16 require the DUT PFW UART command handler (FW-2) to be
flashed.  Until FW-2 is implemented and flashed, all UART steps will time out
and record as FAIL.

### Uploading the recipe

The recipe JSON lives in the repo at `recipes/g3-mb-v1.json`.  After an OTA
firmware update, upload the recipe to LittleFS with:

```bash
python3 scripts/upload_recipe.py recipes/g3-mb-v1.json
```

This uses the `recipe set-b64 <id> <base64>` console command (requires
firmware with `max_cmdline_length = 4096`, set since v1.2.0).

To run the recipe manually from the TCP console:

```
recipe run g3-mb-v1
```

To set it as the active recipe (run automatically on `sm start`):

```
nvs_set recipes active str g3-mb-v1
```

### Recipe version history

| Version | Date | Change |
|---------|------|--------|
| 1.0.0 | 2026-03-18 | Initial recipe — TC carrier steps only |
| 1.1.0 | 2026-03-19 | Add DUT UART steps (disabled), heartbeat, dut_program |
| 1.2.0 | 2026-03-21 | Enable 8 DUT UART steps (enter→exit); heartbeat + program remain disabled |

---

## Provisioning (commissioning identity)

TC identity is provisioned at commissioning time via the `nvs_set` DCMD.  Once
set, the provisioned values persist across reboots and OTA updates (NVS survives
firmware flash).

### Identity keys

| NVS namespace | Key | Description |
|---|---|---|
| `provision` | `fixture_id` | Opaque fixture identifier (shown in NBIRTH) |
| `provision` | `group_id` | Sparkplug B group_id — replaces `SensitMfg` in topic |
| `provision` | `node_id` | Sparkplug B node_id — replaces TC serial in topic |
| `provision` | `ota_root_ca` | PEM root CA for HTTPS OTA (up to ~4 KB) |

### Provisioning DCMD

Send as a Sparkplug B DCMD to the TC's DCMD topic (before or after provisioning
— the `nvs_set` handler is always active):

```json
{"cmd": "nvs_set", "namespace": "provision", "key": "group_id", "value": "SensitProd"}
{"cmd": "nvs_set", "namespace": "provision", "key": "node_id",  "value": "TC-001"}
{"cmd": "nvs_set", "namespace": "provision", "key": "fixture_id", "value": "FX-042"}
```

**Identity keys** (`group_id`, `node_id`, `fixture_id`) take effect immediately
in-memory and trigger an automatic **REBIRTH** — the TC republishes NBIRTH with
the updated identity.  REBIRTH does not reset the test state machine.

**`ota_root_ca`** stores the full PEM cert from the raw DCMD payload (no
length truncation).  Requires a reboot to take effect.

### NBIRTH identity metrics

| Metric | Value when provisioned | Value when unprovisioned |
|---|---|---|
| `Properties/HwUid` | ESP32 MAC (always) | ESP32 MAC (always) |
| `Properties/FixtureId` | provisioned `fixture_id` | `""` (empty string) |

### Topic routing

Provisioned TC topics use the provisioned identity:

```
spBv1.0/<group_id>/NBIRTH/<node_id>/CH<N>
```

Unprovisioned fallback:

```
spBv1.0/SensitMfg/NBIRTH/<serial>/CH<N>
```

where `<serial>` is the TC hardware serial (e.g. `G3-MB-Tester-001`).

---

## LBB transport (USB-Serial-JTAG)

The **Local Bus Bridge (LBB)** transport exposes TC telemetry over the USB-
Serial-JTAG port.  It is the only data path for TCs installed inside metal
enclosures (WiFi blocked).

### Enabling / disabling

```
mqtt lbb on     # enable (persists in NVS device/lbb; reboot to apply)
mqtt lbb off    # disable
mqtt lbb        # show current state
```

!!! note "sdkconfig"
    For a clean NDJSON channel with no ESP-IDF log noise, set
    `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` in `sdkconfig.esp32-Devkit`.
    Without this, the secondary UART console output may interleave with LBB
    frames.  This is a T1 pre-production coordination item.

### Wire format

**TC → T1 (outbound):** NDJSON envelope wrapping the standard Sparkplug payload:

```
{"t":"DDATA","ch":0,"p":{...}}\n
```

- `t` — message type (`DDATA`, `NBIRTH`, `NDEATH`)
- `ch` — channel index (0–7)
- `p` — existing JSON payload (same schema as MQTT)

**T1 → TC (inbound):** Raw DCMD JSON (no envelope):

```
{"cmd":"start"}\n
{"cmd":"nvs_set","namespace":"provision","key":"group_id","value":"SensitProd"}\n
```

All messages are newline-delimited.  Maximum inbound line length: **8192 bytes**
(supports 4 KB base64-encoded OTA chunks).

---

## Path C — Chunked OTA over LBB

TCs installed inside metal enclosures have no WiFi.  The standard URL-based OTA
(`esp_https_ota`) is architecturally unavailable.  **Path C** transfers firmware
as base64-encoded chunks over the LBB USB-Serial-JTAG port.

### TC firmware receiver

The `ota_chunk` DCMD handler in `common/src/cmd_ota.c` implements the receiver:

```json
{"cmd":"ota_chunk","seq":0,"offset":0,"total":131072,"data":"<base64>","sha256":"<hex>"}
```

| Field | Type | Description |
|---|---|---|
| `seq` | int | Zero-based chunk sequence number |
| `offset` | int | Byte offset of this chunk in the full binary |
| `total` | int | Total firmware size in bytes |
| `data` | string | Base64-encoded chunk payload |
| `sha256` | string | Full-image SHA-256 hex digest (present on last chunk only) |

**Session flow:**

1. First chunk (`seq=0`): calls `esp_ota_begin()` on the next OTA partition
2. Each chunk: base64-decode, `esp_ota_write()`, accumulate SHA-256
3. Last chunk (when `offset + decoded_len >= total`): verify SHA-256, call
   `esp_ota_end()` + `esp_ota_set_boot_partition()`, reboot
4. Seq mismatch or SHA-256 mismatch: abort session, rollback partition unchanged

TC publishes `{"type":"ota_progress","seq":N,"written":N}` DDATA after each
chunk and `{"type":"ota_result","result":"ok"}` before rebooting.

### Pi sender script

```bash
python3 scripts/ota_chunk_send.py \
    --port /dev/ttyUSB-ch1 \
    --firmware .pio/build/esp32-Devkit/firmware.bin
```

Options:

| Flag | Default | Description |
|---|---|---|
| `--port` | required | Serial device for target TC channel |
| `--firmware` | required | Path to firmware.bin |
| `--chunk-size` | `3072` | Bytes per chunk (base64 ~4096 + envelope ≤ 8192) |
| `--baud` | `115200` | Serial baud rate |

The script computes the full-image SHA-256 before sending, includes it on the
last chunk, and waits for NBIRTH after reboot to confirm the update landed.

### mark_valid — LBB-only health gate

Post-OTA, the firmware runs a health check before calling
`esp_ota_mark_app_valid_cancel_rollback()`.  The standard gate polls for WiFi
+ MQTT connectivity.  LBB-only TCs (no broker configured) would always time
out and roll back.

**Fix:** On boot, if `NVS device/lbb=1` AND `NVS mqtt/broker_url` is empty,
the health task marks valid **immediately** and exits.  No 90-second wait.

### Chunk sizing

| Parameter | Value | Reason |
|---|---|---|
| Chunk payload | 3072 bytes | Base64 = 4096 chars |
| JSON envelope overhead | ~100 chars | `{"cmd":"ota_chunk","seq":...}` |
| Total line length | ~4200 chars | Well within LBB_RX_LINE_MAX (8192) |

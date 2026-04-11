> Extends: ../../AGENTS.md

# AGENTS.md — embedded/tester-client

TC (Test Controller) firmware for the G3 Main Board Tester. Runs on ESP32-S3 (Freenove ESP32-S3 WROOM N16R8). Built with PlatformIO + ESP-IDF framework.

---

## PlatformIO Environments

| Env | Purpose | Use When |
|-----|---------|---------|
| `esp32-Devkit` | Primary production build (default) | All normal builds and flashing |
| `esp32-Devkit-Debug` | Verbose logging, `-O0` | Debugging only |
| `uno_r4_wifi` | Arduino Uno R4 WiFi (alternative platform) | Platform evaluation only |
| `native` | Linux native, mock HAL, CI | Unit tests without hardware |

Default env: `esp32-Devkit`

---

## Flash Method

```bash
# USB CDC — preferred (faster, no adapter needed)
pio run -e esp32-Devkit -t upload --upload-port /dev/ttyACM0

# UART adapter — fallback
pio run -e esp32-Devkit -t upload --upload-port /dev/ttyUSB0
```

Do not require a UART adapter when USB CDC is available. Do not require physical power cycling or button presses — the device is always recoverable remotely.

---

## Source Layout

| Directory | Contents |
|-----------|----------|
| `src/` | Top-level main entry point (`main.cpp`), recipe engine, DUT detect/identify, TC config |
| `app/` | Application-layer modules (library for PlatformIO) |
| `bsp/` | Board support package — G3 TC hardware abstraction |
| `boards/` | Board definition JSON (`freenove_esp32_s3_wroom_n16r8.json`) |
| `common/` | Submodule: `tc-firmware-common` — shared firmware library across all fixtures |
| `components/` | ESP-IDF components |
| `recipes/` | Test recipe definitions (library) |
| `storage/` | NVS and LittleFS storage layer (library) |
| `dut/` | DUT interface layer (library) |
| `test/` | Unity unit tests (native env) |

`common/` is a git submodule pointing to `INTenX/tc-firmware-common`. Include paths: `common/include`, `common/hal`, `common/src`, `common/storage`.

### common/ Submodule — extern bridge pattern

`common/` modules (e.g. `tc_statemachine.c`) cannot `#include` headers from `src/`. Bridge by declaring `extern void fn()` in the common module and providing the implementation in a new `src/*.c` file. Examples: `tc_sm_spawn_recipe_task()` → `src/recipe_task.c`; `g3_critical_abort()` → `src/g3_primitives.c`.

---

## TCP Console

The firmware exposes a TCP console on port **4242** at the device IP. Connect with:
```bash
python3 -c "import socket; s=socket.socket(); s.connect(('<device-ip>', 4242)); s.settimeout(90)"
```

Useful for command interaction and log capture without a serial cable.

### Monitor test traffic

```bash
bash scripts/monitor_tc.sh   # run from embedded/tester-client/
```

Streams TC console output until the state machine reaches a terminal state (Pass/Fail/Idle). Wraps `tcp_console.py monitor`. Use this to watch recipe runs started from the UI.

---

## PSRAM Constraints

- PSRAM: 8MB OPI (accessible after `extram_add_to_heap_allocator` at boot)
- **Never use GPIO35 or GPIO36** — these are OPI PSRAM data lines. Using them as GPIO will corrupt PSRAM.
- `SPIRAM_BOOT_HW_INIT` must remain explicitly disabled in `sdkconfig.esp32-Devkit` — PlatformIO may re-enable it, which causes boot failures.

---

## DUT Firmware Partition

- Partition name: `dut_fw`
- Size: **896 KB** at offset **0xE0000**
- This is NOT 1 MB — do not use 1 MB as the partition size in any tooling or validation.

---

## SWD (DUT Programming from TC)

- Bit-bang SWD implemented in firmware (`SWD_FAST_GPIO` build flag)
- Target: STM32 on DUT (UID96: `49002E000450325334383820` — reference unit)
- Command: `swd_flash <path>` via TCP console or MQTT DCMD
- Validated: 5/5 flash cycles successful

---

## Partition Table

Uses `partitions_ota.csv` (OTA + LittleFS recipe storage). Do not use the default partition table.

## Non-Destructive USB Flash

`pio run -t upload` writes ONLY bootloader (0x0), partition table (0x8000), and factory app slot (0x20000). All provisioning data survives: NVS (WiFi, broker_url, calibration, device UID @ 0x9000), dut_fw (0x620000), prod_fw (0x6C0000), recipes LittleFS (0x7A0000). Nothing needs re-provisioning if partition table offsets are unchanged.

After USB flash, otadata **must be erased** so the bootloader falls back to factory instead of a stale OTA slot:

```bash
# Pre-flash: check current OTA state
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --port /dev/ttyACM0 read_flash 0xf000 0x2000 /tmp/otadata.bin
python3 scripts/decode_otadata.py /tmp/otadata.bin
# Flash factory slot
pio run -t upload
# Clear OTA state so bootloader boots factory
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --port /dev/ttyACM0 erase_region 0xf000 0x2000
```

`esptool.py --erase-all` or full chip erase destroys NVS (WiFi, broker_url, calibration, device UID) — never use before USB flash.

---

## Hardware Facts

**GPIO pin map (confirmed, do not re-derive):**

| Signal | GPIO | Notes |
|--------|------|-------|
| SWCLK | 45 | Free strapping pin; `tcc_pinmap.h` `TCC_SWD_SWCLK_GPIO` |
| SWDIO | 38 | `TCC_SWD_SWDIO_GPIO` |
| MUX SIG (U8 HEF4051) | 0 | Reworked from GPIO36 (HW-011); strapping pin, internal pull-up — safe because all U8 loads are high-impedance at boot |
| VDUT1 PWM | 1 | LEDC_CHANNEL_0 → RC filter → MP2315SGJ-Z feedback (HW-013 rework; was GPIO19) |
| VDUT2 PWM | 2 | LEDC_CHANNEL_1 → RC filter → MP2315SGJ-Z feedback (HW-013 rework; was GPIO20) |
| I2C SDA | 15 | Shared bus: ADC128D818, INA219×2, PCF8574 LCD |
| I2C SCL | 16 | 400 kHz fast mode |
| DUT UART RX | 44 | Monitor line; DUT UART TX monitored |
| OPI PSRAM data | 35, 36 | MSPI D6/D7 — NEVER use as GPIO |
| MUX address | 3–6, 7–10, 11–14, 39–42 | Four HEF4067BTT MUXes (U3/U11/U12/U1); see `tie_pinmap.h` |

**ADC VREF:** `ADC128_VREF_MV = 3000` mV (MAX6103 precision reference, `tie_pinmap.h`). 12-bit full-scale = 4096 counts.

**DUT presence detection:** ADC128D818 CH3 via MUX ch3. `DUT_DETECT_THRESHOLD_MV = 2000 mV`. **Below threshold = DUT present; above = absent.** Typical seated reading ≈1666 mV.

**PB-A (power button assert):** `mux select 0 0` selects HEF4051 U8 channel 0 (A0=0, A1=0, A2=0), routing SIG (GPIO0) to the DUT KEEPALIVE latch. `mux release` deasserts.

**VDAC inverting:** Higher LEDC duty = lower output voltage. Always use `vdac_set_voltage` (converts via calibration), never call `vdac_set_duty` directly from application code.

---

## MQTT Contract (Sparkplug B)

**Topic structure:** `spBv1.0/{group_id}/{type}/{node_id}/CH{channel}`

- `group_id`: from NVS `provision/group_id`; defaults to `SensitMfg`
- `node_id`: from NVS `provision/node_id`; defaults to `s_serial`
- `channel`: 0-indexed fixture channel (set via `tc_mqtt_configure`)

**Published types:**

| Type | QoS | Notes |
|------|-----|-------|
| NBIRTH | 1 | On broker connect; firmware versions + initial state |
| NDEATH | 1 | Before `esp_restart()` — call `tc_mqtt_publish_ndeath()` |
| DDATA/state | 0 | State machine transitions |
| DDATA/result | 1 | Test outcome; NOT gated on clock validity |
| DDATA/step_result | 0 | Per-step after execution |
| DDATA/test_progress | 0 | Live step progress |
| DDATA/selftest | 1 | After `selftest` command |
| DDATA/dut_presence | 0 | Dedicated plain-MQTT topic (not Sparkplug DDATA) |

**DCMD subscribe:** `spBv1.0/SensitMfg/DCMD/{serial}` (node-level) and `spBv1.0/SensitMfg/DCMD/{serial}/CH{channel}` (device-level).

**Clock gate rule:** Result publish is NOT gated on NTP clock validity — publish regardless. NBIRTH IS gated on a valid timestamp.

**NDEATH rule:** Always call `tc_mqtt_publish_ndeath()` immediately before `esp_restart()`. No-op if not connected — safe to call unconditionally.

---

## Recipe JSON Schema

Top-level fields:

```json
{
  "recipe_id": "g3-mb-production-v1",   // string, key for NVS storage
  "recipe_version": "1.0.0",            // semver string (16 chars max)
  "name": "G3 Main Board Production",   // human-readable name (64 chars max)
  "timeout_ms": 120000,                 // overall recipe timeout
  "steps": [...],                       // see per-step schema below
  "recovery_branches": [...]            // optional named recovery sequences
}
```

Per-step fields:

```json
{
  "id": "power_on",                     // string ID (32 chars max) — used in result/log
  "primitive": "vdut_enable",           // registered primitive name (32 chars max)
  "label": "Power On DUT",             // display label (48 chars max)
  "status_msg": "Applying 3.3V...",    // optional MQTT status override; defaults to label
  "criticality": "required",           // "required" (default) | "critical" | "optional"
  "on_error": "abort",                 // "abort" | "skip" | "recovery:<name>"
  "enabled": true,                     // false = skip unconditionally
  "requires_pass": false,              // true = skip if any REQUIRED step has failed
  "params": {}                         // primitive-specific parameters
}
```

**Criticality semantics:**
- `required` (default): record fail, continue for diagnostics
- `critical`: abort immediately, cut VDUT within 100 ms
- `optional`: advisory only, never affects pass/fail outcome

**Limits:** `JSON_RECIPE_STEPS_MAX = 140`. If a recipe exceeds this, steps are silently truncated (see PAT-069). Recovery branches: max 8 branches, 8 steps each.

**NVS storage:** Stored as raw JSON string; key = `recipe_id`. Load via `recipe_json_load_nvs(recipe_id)`. Active recipe set via `recipe set <id>` console command.

---

## BATON CLI

BATON is the inter-agent alert/message bus. Full path: `/home/cbasta/rtgf-ai-stack/baton/baton.sh`

```bash
# List pending messages for this agent
bash /home/cbasta/rtgf-ai-stack/baton/baton.sh list

# Post an alert (type: alert | info | question | handoff)
bash /home/cbasta/rtgf-ai-stack/baton/baton.sh alert "message text"
bash /home/cbasta/rtgf-ai-stack/baton/baton.sh info "message text"

# Mark a message as read
bash /home/cbasta/rtgf-ai-stack/baton/baton.sh ack <message-id>
```

Store path: `~/rtgf-agents/agents/{slug}/inbox/`. Check BATON at session start (step 2 of Session Start Checklist).

---

## SWD and Deployment Tips

**SWD flash (nopwrcycle):** When re-flashing the DUT without cutting power, pass `nopwrcycle` to the SWD flash command to skip the power cycle that would drop PBA:
```
swd_flash <path> nopwrcycle
```

**Push recipe via MQTT (no console required):**
```bash
python3 scripts/upload_recipe.py --broker 10.0.0.1 --recipe recipes/g3_production.json
```

**TC mDNS:** The TC registers as `g3-tester-{serial}.local` via mDNS. Use instead of hardcoding the IP:
```bash
python3 scripts/tcp_console.py g3-tester-G3-MB-001.local
```
Do not hardcode IPs in scripts — use `tc_config.py` for host resolution (ADR-013).

**OTA from WSL:** Use `serve_firmware.sh` with `run_in_background` flag; point to WSL LAN IP (not tunnel interface). See PAT-076 for WSL IP detection issue with `swd_store_pfw.py`.

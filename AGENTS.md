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

---

## TCP Console

The firmware exposes a TCP console on port **4242** at the device IP. Connect with:
```bash
python3 -c "import socket; s=socket.socket(); s.connect(('<device-ip>', 4242)); s.settimeout(90)"
```

Useful for command interaction and log capture without a serial cable.

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

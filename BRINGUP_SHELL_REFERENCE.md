# G3-TC Bringup Shell — Command Reference

**Firmware:** G3 TC Bringup Shell
**Target:** ESP32-S3 (Freenove N16R8, 16 MB flash) on TCC Carrier Board
**Console:** UART (ttyUSB0 @ 115200) or TCP port 4242 over WiFi

---

## Connecting to the Shell

### USB UART (primary — always available)

```bash
pio device monitor -b 115200 -p /dev/ttyUSB0
# or
screen /dev/ttyUSB0 115200
```

### TCP Console (wireless — requires WiFi connection)

```bash
nc <ESP32_IP> 4242
# or
telnet <ESP32_IP> 4242
```

The TCP console mirrors all output, including `ESP_LOG*` messages. All commands work identically over both transports. Press `Ctrl+C` or close the TCP connection to disconnect; the device keeps running.

### Prompt Format

```
g3-tc|0.1.1>
```

Version string is embedded at build time. Format: `<fw_version.txt>+<git-commit-count>`.

---

## Command Overview

| Command | Subcommands | Hardware |
|---------|------------|---------|
| `gpio` | `set get mode` | Any GPIO pad |
| `pwm` | `set stop status` | LEDC peripheral, RC-filter DAC outputs |
| `adc` | `read` | ADC1 (GPIO1–GPIO10) |
| `i2c` | `init scan read write` | I2C bus (SDA=GPIO15, SCL=GPIO16) |
| `wifi` | `scan connect disconnect status forget` | ESP32 WiFi STA, NVS |
| `ota` | `update status` | OTA flash partitions, HTTPS |
| `selftest` | `i2c adc wifi ota all` | All of the above |
| `help` | — | — |

---

## `gpio` — GPIO Control

Drive or read any GPIO pad. Used for relay control, branch select switches, power enable pins, and LED verification.

### `gpio set <pin> <0|1>`

Configure pad as output and drive it high or low.

```
gpio set 5 1          → GPIO5 -> 1
gpio set 5 0          → GPIO5 -> 0
```

**Note:** Driving a GPIO that is also used by I2C (GPIO15, GPIO16) will corrupt the I2C bus IO_MUX. Run `i2c init` afterward to restore.

### `gpio get <pin>`

Configure pad as input and read the current level.

```
gpio get 5            → GPIO5 = 1
gpio get 21           → GPIO21 = 0
```

### `gpio mode <pin> <in|out|od>`

Set direction without driving a level.

```
gpio mode 5 out       → GPIO5 mode -> out
gpio mode 5 in        → GPIO5 mode -> in
gpio mode 5 od        → GPIO5 mode -> od   (open-drain)
```

### Typical Bringup Sequence

```
gpio mode 5 out
gpio set 5 1          # enable relay — expect click/LED change
gpio get 5            # read back to confirm
gpio set 5 0          # release relay
```

---

## `pwm` — PWM / LEDC Output

Generates PWM signals on any GPIO using the ESP32 LEDC peripheral. Up to 6 channels active simultaneously. All channels share a single timer (LEDC_TIMER_0, 12-bit resolution).

On the TCC carrier, PWM pins drive RC-filtered DAC outputs for adjustable voltage/current control of the DUT.

### `pwm set <gpio> <freq_hz> <duty_pct>`

Start or update PWM on a GPIO. `duty_pct` is 0–100. Calling `set` on a gpio that already has PWM updates the duty/frequency without releasing the channel.

```
pwm set 18 1000 50    → PWM GPIO18: 1000 Hz  50%  (raw duty=2047)
pwm set 18 1000 75    → PWM GPIO18: 1000 Hz  75%  (raw duty=3071)
pwm set 19 10000 25   → PWM GPIO19: 10000 Hz  25%  (raw duty=1023)
```

**Target voltage after RC filter:**
`V_out ≈ duty_pct/100 × V_supply`. At 3.3V supply: 50% → ~1.65V, 75% → ~2.475V.
Allow ≥10ms settling time after duty change before measuring.

### `pwm stop <gpio>`

Stop PWM on a GPIO and free the LEDC channel.

```
pwm stop 18           → PWM GPIO18 stopped
```

### `pwm status`

List all active PWM channels.

```
pwm status
  CH0: GPIO18
  CH1: GPIO19
```

---

## `adc` — ADC Read

Reads ADC1 channels (GPIO1–GPIO10 on ESP32-S3). Uses eFuse-based curve-fitting calibration where available; falls back to line-fitting or raw counts.

Attenuation: 12 dB (0–3.3V range).

### `adc read <ch> [ch...]`

Read one or more channels. Prints raw counts (0–4095) and calibrated millivolts.

```
adc read 0            → ADC1 CH0: raw=412    123 mV
adc read 1 2 3        → ADC1 CH1: raw=2048  1650 mV
                         ADC1 CH2: raw=4012  3280 mV
                         ADC1 CH3: raw=0        0 mV
```

**Channel-to-GPIO mapping (ESP32-S3):**

| Channel | GPIO |
|---------|------|
| 0 | GPIO1 |
| 1 | GPIO2 |
| 2 | GPIO3 |
| 3 | GPIO4 |
| 4 | GPIO5 |
| 5 | GPIO6 |
| 6 | GPIO7 |
| 7 | GPIO8 |
| 8 | GPIO9 |
| 9 | GPIO10 |

**Note:** ADC readings above ~3.1V saturate. For higher rails, use a resistor divider and scale the result.

---

## `i2c` — I2C Master Bus

Master bus operations on I2C_NUM_0. Default pins: SDA=GPIO15, SCL=GPIO16 (TCC carrier wiring). All addresses and register values are hex.

**TCC carrier devices:**

| Address | Device | Purpose |
|---------|--------|---------|
| `0x1D` | ADC128D818 | 8-channel analog input ADC |
| `0x40` | INA219 #0 | Current sense — power rail A |
| `0x41` | INA219 #1 | Current sense — power rail B |

### `i2c init [sda] [scl] [hz]`

Initialize (or re-initialize) the I2C bus. Without arguments, uses defaults (GPIO15, GPIO16, 400 kHz). Always run this after using `gpio set/mode` on GPIO15 or GPIO16.

```
i2c init                       → I2C ready: SDA=GPIO15  SCL=GPIO16  400kHz
i2c init 15 16 100000          → I2C ready: SDA=GPIO15  SCL=GPIO16  100kHz
```

### `i2c scan`

Probe all 7-bit addresses (0x00–0x7F). Prints a grid; found devices shown as their hex address.

```
i2c scan
Scanning I2C (SDA=GPIO15 SCL=GPIO16)...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- 1d -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: 40 41 -- -- -- -- -- -- -- -- -- -- -- -- -- --
...
3 device(s) found
```

**Note:** Address `0x00` (general call) may show as found — this is a quirk of the I2C general call address, not a real device.

### `i2c read <addr> <reg> <nbytes>`

Read N bytes from a register. All values in hex. `nbytes` range: 1–32.

```
i2c read 1d 00 2      → 0x1d [reg 0x00, 2 bytes]: 00 00
i2c read 40 02 2      → 0x40 [reg 0x02, 2 bytes]: 61 80
i2c read 41 05 2      → 0x41 [reg 0x05, 2 bytes]: 00 50
```

### `i2c write <addr> <reg> <b0> [b1...]`

Write one or more bytes to a register. All values in hex. Max 31 data bytes.

```
i2c write 1d 08 01    → 0x1d [reg 0x08] <- 01  OK
i2c write 40 00 39 9f → 0x40 [reg 0x00] <- 39 9f  OK
```

---

## `wifi` — WiFi Station

Manages the ESP32 WiFi STA connection. Credentials are persisted to NVS (namespace `wifi_cfg`) and survive OTA updates and reboots. Auto-connect fires on every boot if credentials are saved.

### `wifi scan`

Passive scan for access points. Prints SSID, channel, and RSSI.

```
wifi scan
SSID                              CH  RSSI
--------------------------------  --  ----
MyLabAP                            6   -42
NeighborNet                       11   -78

2 AP(s) found
```

### `wifi connect <ssid> <password>`

Connect to an AP and save credentials to NVS. Waits up to 10 seconds for connection. IP address is printed on success.

```
wifi connect MyLabAP hunter2
Connecting to 'MyLabAP'...
WiFi connected — IP: 192.168.1.105
```

After a successful connect the TCP console (port 4242) becomes available.

### `wifi disconnect`

Disconnect from the current AP. Does not erase saved credentials.

```
wifi disconnect       → Disconnected
```

### `wifi status`

Show current connection state, IP, and RSSI. If not connected, shows the saved SSID if one exists.

```
wifi status
SSID    : MyLabAP
RSSI    : -52 dBm
Channel : 6
IP      : 192.168.1.105
GW      : 192.168.1.1
```

```
wifi status           # when not connected
Not connected
Saved   : MyLabAP
```

### `wifi forget`

Erase saved credentials from NVS and disconnect. After this the device will not auto-connect on reboot.

```
wifi forget           → Credentials cleared
```

---

## `ota` — OTA Firmware Update

Downloads and flashes firmware from an HTTP or HTTPS URL. Uses the Mozilla CA bundle (`esp_crt_bundle`) for HTTPS certificate validation. Supports GitHub private release assets via bearer token.

After a successful flash the device reboots automatically. The TCP console reconnects once WiFi is restored (~8 seconds).

Partition layout: `factory` (never overwritten) → `ota_0` → `ota_1` → `ota_0` → …

### `ota update <url> [bearer_token]`

Download and flash firmware from URL.

```
# Local HTTP server (no auth):
ota update http://192.168.1.20:8080/firmware.bin

# GitHub private release with Personal Access Token:
ota update https://api.github.com/repos/ORG/REPO/releases/assets/12345678 ghp_YourToken

# Pre-signed CDN URL (no token needed, short-lived):
ota update https://release-assets.githubusercontent.com/...?token=XXX...
```

**Expected output:**
```
OTA: fetching http://192.168.1.20:8080/firmware.bin
OTA complete — rebooting in 1 second
[connection closed — device rebooted]
```

**On failure** (no reboot, device keeps running):
```
OTA: fetching http://192.168.1.20:8080/firmware.bin
OTA failed: ESP_ERR_OTA_VALIDATE_FAILED
```

**Prerequisites for local HTTP:**
1. Python HTTP server running on dev machine: `python3 -m http.server 8080 --directory .pio/build/esp32-Devkit/`
2. Windows Firewall inbound rule allowing TCP 8080 (WSL2 only)
3. Use the Windows host LAN IP, not the WSL internal IP (check with `ip route show default`)

### `ota status`

Show running partition, firmware version, build date, and OTA image lifecycle state.

```
ota status
Partition : ota_0
Version   : 0.1.1+35
Build     : Mar  4 2026 14:22:08
IDF ver   : v5.5.0
OTA state : valid
```

**OTA state values:**

| State | Meaning |
|-------|---------|
| `undefined` | Factory partition — normal, not managed by OTA rollback |
| `valid` | OTA image confirmed good by rollback guard on boot |
| `pending_verify` | Waiting for app to call `mark_app_valid` (transient — only seen briefly) |
| `new` | Freshly written, not yet booted |
| `invalid` | Marked bad — bootloader will not boot this slot |
| `aborted` | App crashed before confirming — bootloader rolled back |

---

## `selftest` — Bringup Selftest

Runs hardware checks against TCC carrier devices and reports PASS/FAIL per subsystem. Tests are non-destructive — no relays are toggled, no output pins are driven.

Run individual subsystems or all at once.

### `selftest all` (or just `selftest`)

```
selftest all
=== G3-TC Bringup Selftest ===
[PASS] I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)
[PASS] I2C: ADC128D818 @ 0x1D
[PASS] I2C: INA219 #0  @ 0x40
[PASS] I2C: INA219 #1  @ 0x41
[PASS] ADC:  CH0 (GPIO1) raw=412    123 mV
[PASS] WiFi: connected  SSID=MyLabAP  RSSI=-52 dBm
[PASS] OTA:  partition=ota_0    state=valid           fw=0.1.1+35

Results: 7/7 passed
```

### `selftest i2c`

Initializes the bus (if not already initialized) and probes the three expected TCC carrier devices.

```
selftest i2c
[PASS] I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)
[PASS] I2C: ADC128D818 @ 0x1D
[PASS] I2C: INA219 #0  @ 0x40
[PASS] I2C: INA219 #1  @ 0x41

Results: 4/4 passed
```

**FAIL on device probe** means the device is not responding. Check:
- TCC carrier powered (VCC present at carrier header)
- Back-power path eliminated (only one 3.3V source driving the rail)
- I2C pull-ups present (enabled internally by firmware)

### `selftest adc`

Reads ADC1 CH0 (GPIO1) and verifies the read succeeds.

```
selftest adc
[PASS] ADC:  CH0 (GPIO1) raw=412    123 mV

Results: 1/1 passed
```

A `raw=0` or `raw=4095` result is valid — it just means GPIO1 is at 0V or at the ADC rail. The test passes as long as the read completes without error.

### `selftest wifi`

Checks if the STA interface is currently associated with an AP.

```
selftest wifi
[PASS] WiFi: connected  SSID=MyLabAP  RSSI=-52 dBm

Results: 1/1 passed
```

```
selftest wifi
[FAIL] WiFi: not connected  (run: wifi connect <ssid> <pass>)

Results: 0/1 passed
```

WiFi FAIL is expected before credentials are configured. This is not a firmware bug.

### `selftest ota`

Checks the running partition label and OTA image lifecycle state.

```
selftest ota
[PASS] OTA:  partition=ota_0    state=valid           fw=0.1.1+35

Results: 1/1 passed
```

PASS on `state=undefined` (factory) or `state=valid`. FAIL on `invalid` or `aborted` — these indicate a problematic OTA image was detected.

---

## Manual Bringup Checklist

Use this sequence to bring up a freshly-assembled TCC carrier for the first time.

### Phase 0 — Pre-Power (multimeter, no firmware)

- [ ] No short: VIN to GND on carrier barrel jack
- [ ] No short: 3.3V to GND post-regulator
- [ ] No short: 5V to GND post-regulator

### Phase 1 — Flash and Boot

```bash
~/.local/bin/pio run -e esp32-Devkit -t upload --upload-port /dev/ttyUSB0
pio device monitor -b 115200 -p /dev/ttyUSB0
```

Expected: No panic, no watchdog reset. Version string and I2C scan attempt visible in boot log.

### Phase 2 — I2C Peripherals

```
i2c init
selftest i2c
```

Expected: 3 devices found (0x1D, 0x40, 0x41). All PASS.

### Phase 3 — ADC

```
selftest adc
adc read 0 1 2 3
```

Expected: Reads return. Values depend on what's connected to ADC1 pins.

### Phase 4 — WiFi and Remote Console

```
wifi connect <ssid> <password>
wifi status
```

Expected: IP assigned within 10 seconds. Then from dev machine:

```bash
nc <ESP32_IP> 4242
```

### Phase 5 — Full Selftest

```
selftest all
```

Expected: 7/7 passed (WiFi must be connected for WiFi test to pass).

### Phase 6 — OTA Smoke Test

```
ota status
```

Expected: partition=factory (first boot), state=undefined, version matches built firmware.

Optionally run an OTA update from local server to confirm end-to-end update path works before the board goes into the test fixture.

---

## Troubleshooting

### I2C scan shows no devices or wrong addresses

1. Verify TCC carrier is powered — ESP32 alone will not see carrier devices
2. Check for back-power conflict: only one voltage source may drive the 3.3V rail
3. Run `i2c init` to reset the bus — any previous `gpio set/mode` on GPIO15/16 corrupts I2C IO_MUX
4. Reduce speed: `i2c init 15 16 100000`

### `wifi connect` times out

- Verify SSID and password are correct (`wifi scan` to confirm AP visible)
- Check AP is on 2.4 GHz (ESP32 does not support 5 GHz)
- Distance/RSSI — move board closer to AP

### OTA fails with `ESP_ERR_HTTP_CONNECT`

- WiFi not connected — run `wifi status` first
- Local HTTP: check Windows Firewall allows inbound TCP 8080
- Local HTTP: use Windows host LAN IP, not WSL internal IP (`ip route show default` shows it)

### OTA fails with `ESP_ERR_OTA_VALIDATE_FAILED`

- Binary is corrupt or wrong target — rebuild with `pio run -e esp32-Devkit`
- Wrong binary (e.g., built for a different board target)

### `nc <ip> 4242` connects then immediately closes

- WiFi is up but the TCP task crashed — check UART log for panic
- Another client is already connected (single-client limit) — disconnect the other session

### Prompt does not appear after boot (UART)

- Check baud rate: must be 115200
- Check `CONFIG_ESP_CONSOLE_UART_DEFAULT` is set (not USB_SERIAL_JTAG) in sdkconfig
- Try pressing Enter — REPL may be waiting for input

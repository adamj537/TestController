# TC Firmware — Maintainer Notes

<!--
  Transcluded into: docs/maintenance/firmware/tc-firmware.md
-->

## Current build

| Parameter | Value |
|---|---|
| Branch | `feat/bringup-shell` (PR #12 open) |
| RAM usage | 4.2% |
| Flash usage | 31.0% (318 KB) |
| Target | ESP32-S3 (Freenove DevKit) |
| Framework | ESP-IDF 5.5.0 |

## Connecting to the bringup shell

Two connection methods are available:

### TCP console (preferred — wireless)

```bash
nc <device-ip> 4242
```

The device IP is printed to the serial console on boot and on each WiFi
reconnect.  The TCP console accepts all bringup shell commands and tees
all `stdout`/`stderr` output to the connected client.

Press **Ctrl-C** to disconnect without affecting the device.

### USB CDC serial (bench, pre-WiFi)

Connect the ESP32-S3 DevKit USB-C port to a host PC.  A CDC serial device
appears (e.g., `/dev/ttyACM0` on Linux, `COMx` on Windows).

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

## Checking firmware version

```
mac
```

The `mac` command prints the firmware version, chip model, flash size, MAC
address, and WiFi status.

## OTA firmware update

```bash
# 1. Build on the host
cd embedded/tester-client
pio run -e esp32-Devkit

# 2. Serve the firmware binary
cd .pio/build/esp32-Devkit
python3 -m http.server 8080

# 3. On the device (via nc or serial)
ota update http://<host-ip>:8080/firmware.bin
```

After a successful OTA, the device reboots automatically.  The new firmware
marks itself valid on first boot (anti-rollback guard).  If the device does
not boot successfully within the watchdog window it rolls back to the
previous partition.

!!! warning
    Stop any existing `http.server` process before starting a new one
    (`kill $(lsof -ti:8080)`).  A stale server from a previous build directory
    will serve the wrong binary.

## Selftest — expected output

Run `selftest all` via the bringup shell.  All 9 checks should pass on a
healthy board with WiFi configured:

```
=== G3-TC Bringup Selftest ===
[PASS] I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)
[PASS] I2C: INA219 #0  @ 0x40
[PASS] I2C: INA219 #1  @ 0x41
[PASS] I2C: ADC128D818 @ 0x1D
[PASS] ADC128: CH4  3.3V rail  3268 mV  (exp 3135–3465 mV)
[PASS] ADC128: CH5  5.0V rail  4925 mV  (exp 4750–5250 mV)
[PASS] ADC:  CH0 (GPIO1) raw=2048  1234 mV
[PASS] WiFi: connected  SSID=<network>  RSSI=-55 dBm
[PASS] OTA:  partition=ota_0  state=valid  fw=<version>

Results: 9/9 passed
```

!!! note "WiFi check"
    The WiFi test reports FAIL if WiFi credentials have not been configured.
    Run `wifi connect <ssid> <password>` once; credentials are stored in NVS
    and reconnect automatically on subsequent boots.

## Individual selftest groups

```
selftest i2c     # I²C bus and device probe only
selftest adc     # Internal ESP32 ADC (GPIO1)
selftest wifi    # WiFi connection status
selftest ota     # OTA partition and firmware state
```

## Emergency recovery

If the ESP32 becomes unresponsive (no serial output, no TCP console), see
`embedded/tester-client/EMERGENCY_RECOVERY.md` for the manual flash procedure
using PlatformIO and a USB cable.

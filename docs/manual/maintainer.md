# TC Firmware — Maintainer Notes

<!--
  Transcluded into: docs/maintenance/firmware/tc-firmware.md
-->

## Current build

| Parameter | Value |
|---|---|
| Branch | `feat/domain-context-files` |
| Version | 0.1.2+87-dev |
| RAM usage | 11.9% |
| Flash usage | 55.8% (1,171 KB) |
| Target | ESP32-S3 (Freenove DevKit, N16R8) |
| Framework | ESP-IDF 5.5.0 |
| PSRAM | 8 MB Quad SPI (APS6408L, deferred init) |

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

### Quick selftest (precheck)

Run `selftest quick` — 8 checks, ~300 ms:

```
[PASS] I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)
[PASS] I2C: INA219 #0  @ 0x40
[PASS] I2C: INA219 #1  @ 0x41
[PASS] I2C: ADC128D818 @ 0x1D
[PASS] ADC128: CH4  5.0V rail  4959 mV  (exp 4750–5250 mV)
[PASS] ADC128: CH5  3.3V rail  3243 mV  (exp 3135–3465 mV)
[PASS] ADC128: CH7  temp sensor  26.5°C  (±2°C, exp -10–85°C)
[PASS] WiFi: connected  SSID=<network>  RSSI=-50 dBm

Results: 8/8 passed
```

### Full selftest (fixture mode)

Run `selftest all` — 20 checks, ~7 s (includes VDUT sweep, mux scan, DUT
heartbeat).  This is the same set run during `sm start` Testing phase:

```
[PASS] I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)
[PASS] I2C: INA219 #0  @ 0x40
[PASS] I2C: INA219 #1  @ 0x41
[PASS] I2C: ADC128D818 @ 0x1D
[PASS] ADC128: CH4  5.0V rail  4959 mV  (exp 4750–5250 mV)
[PASS] ADC128: CH5  3.3V rail  3243 mV  (exp 3135–3465 mV)
[PASS] ADC128: CH7  temp sensor  27.0°C  (±2°C, exp -10–85°C)
[PASS] ADC:  CH0 (GPIO1) raw=0         0 mV
[PASS] WiFi: connected  SSID=<network>  RSSI=-49 dBm
[PASS] OTA:  partition=factory   state=undefined        fw=<version>
[PASS] VDUT1: duty 10%  INA0_Vbus  9944 mV  (exp 8000-12000)
[PASS] VDUT2: duty 10%  INA1_Vbus  9968 mV  (exp 8000-12000)
[PASS] VDUT1: duty 50%  INA0_Vbus  7000 mV  (exp 5000-9000)
[PASS] VDUT2: duty 50%  INA1_Vbus  7032 mV  (exp 5000-9000)
[PASS] VDUT1: duty 90%  INA0_Vbus  3020 mV  (exp 1000-5000)
[PASS] VDUT2: duty 90%  INA1_Vbus  3044 mV  (exp 1000-5000)
[PASS] VDUT1: monotonic  9944 > 7000 > 3020 mV  (duty 10%→50%→90%)
[PASS] VDUT2: monotonic  9968 > 7032 > 3044 mV  (duty 10%→50%→90%)
[PASS] MUX:  scan  4 × 16 channels
[PASS] DUT:  heartbeat  PA9 toggle 0–2559 mV  (sampled 2.5 s)

Results: 20/20 passed
```

!!! note "WiFi check"
    The WiFi test reports FAIL if WiFi credentials have not been configured.
    Run `wifi connect <ssid> <password>` once; credentials are stored in NVS
    and reconnect automatically on subsequent boots.

!!! note "DUT heartbeat check"
    Requires DUT firmware loaded and running.  The DUT toggles PA9 at 1 Hz;
    the TIE mux routes this to ADC128 CH2 for sampling.

## Individual selftest groups

```
selftest quick       # I²C + rails + temp + WiFi (8 checks, precheck mode)
selftest i2c         # I²C bus and device probe only
selftest adc         # Internal ESP32 ADC (GPIO1)
selftest wifi        # WiFi connection status
selftest ota         # OTA partition and firmware state
selftest temp        # ADC128 CH7 internal temperature
selftest vdut        # 3-point VDUT1/VDUT2 sweep
selftest mux         # TIE analog mux scan
selftest heartbeat   # DUT PA9 1 Hz heartbeat
```

## Emergency recovery

If the ESP32 becomes unresponsive (no serial output, no TCP console), see
`embedded/tester-client/EMERGENCY_RECOVERY.md` for the manual flash procedure
using PlatformIO and a USB cable.

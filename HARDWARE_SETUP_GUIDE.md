# ESP32-DevKit Hardware Setup Guide

**Project:** G3 Main Board Test Fixture - Tester Client Firmware
**Hardware:** Freenove ESP32-S3 WROOM (or compatible ESP32-DevKit)
**Date:** 2026-02-15

---

## Hardware Requirements

### Required Equipment
- **ESP32-DevKit** (ESP32-S3 WROOM or compatible)
- **USB Cable** (USB-A to Micro-USB or USB-C, depending on board)
- **Computer** with PlatformIO installed (Linux/WSL preferred)
- **Optional:** Multimeter for voltage verification

### Test Equipment (for validation)
- Multimeter (voltage/continuity testing)
- Oscilloscope (optional, for UART signal verification)
- Logic analyzer (optional, for protocol debugging)

---

## Physical Connection

### Step 1: Inspect the ESP32-DevKit Board

**Verify the following:**
- [ ] Board is **ESP32-S3** variant (or compatible ESP32)
- [ ] USB connector is intact (no physical damage)
- [ ] Power LED circuit is visible
- [ ] Pins are not bent or damaged

**Board Identification:**
```
Freenove ESP32-S3 WROOM N8R8
- 8MB Flash
- 8MB PSRAM
- 240MHz Xtensa dual-core
```

### Step 2: Connect USB Cable

1. **Power off** the board if previously powered
2. Connect **USB cable** from computer to ESP32-DevKit
3. **Observe power LED** - should illuminate when connected
4. **Wait 2-3 seconds** for USB enumeration

---

## Serial Port Detection

### Linux / WSL

**Find the serial port:**
```bash
# List all USB serial devices
ls -l /dev/ttyUSB* /dev/ttyACM*

# Common ESP32 ports:
# /dev/ttyUSB0  - CH340/CP2102 UART-to-USB
# /dev/ttyACM0  - CDC-ACM (native USB)
```

**Check device info:**
```bash
# View USB device details
lsusb

# Expected output (ESP32-S3):
# Bus 001 Device 010: ID 303a:1001 Espressif ESP32-S3
```

**Grant permissions (if needed):**
```bash
# Add user to dialout group (one-time setup)
sudo usermod -a -G dialout $USER

# OR: Temporarily grant permissions
sudo chmod 666 /dev/ttyUSB0  # Replace with your port
```

### Windows

**Device Manager Method:**
1. Open **Device Manager**
2. Expand **Ports (COM & LPT)**
3. Look for "USB-SERIAL CH340" or "Silicon Labs CP210x"
4. Note the **COM port** (e.g., COM3, COM7)

**PowerShell Command:**
```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'USB' }
```

---

## Driver Requirements

### Linux / WSL
**Most ESP32 boards work out-of-the-box** with built-in kernel drivers:
- CH340: `ch341` driver (usually pre-installed)
- CP2102: `cp210x` driver (usually pre-installed)
- Native USB: `cdc_acm` driver (built into kernel)

**If not detected:**
```bash
# Check kernel modules
lsmod | grep -E "ch341|cp210x|cdc_acm"

# Load module if missing
sudo modprobe ch341  # For CH340 chips
sudo modprobe cp210x # For CP2102 chips
```

### Windows
**Install drivers if needed:**
- **CH340:** [CH340 Windows Driver](http://www.wch.cn/downloads/CH341SER_EXE.html)
- **CP2102:** [Silicon Labs CP210x Driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- **ESP32-S3 Native USB:** Usually works without drivers on Windows 10/11

---

## Board Reset Procedure

### Manual Reset (for flashing)

Some ESP32 boards require **manual boot mode** for flashing:

1. **Press and hold BOOT button** (GPIO0)
2. **Press and release RESET button**
3. **Release BOOT button**
4. Board is now in **download mode** (ready for flashing)

**PlatformIO auto-reset:**
- Most ESP32-DevKit boards support **automatic reset**
- PlatformIO will reset the board automatically during upload
- If auto-reset fails, use manual reset procedure above

### Verify Boot Mode

**Check serial monitor:**
```bash
pio device monitor -b 115200
```

**Expected output in normal boot:**
```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
...
ESP-IDF v5.5.0
```

**Expected output in download mode:**
```
waiting for download
```

---

## Testing Serial Communication

### Basic Serial Test

**Monitor serial port:**
```bash
pio device monitor -b 115200 -e esp32-Devkit
```

**Expected behavior:**
- Board boots and prints ESP-IDF startup messages
- No garbled characters (indicates correct baud rate)
- Continuous output if firmware has serial logging

**If garbled output:**
- Check baud rate matches (115200)
- Verify correct port selected
- Check USB cable quality

---

## Pin Mapping (for reference)

### GPIO Assignments (from firmware)

**DUT Interface Connections:**
- **UART0 TX:** GPIO 1 (DUT communication)
- **UART0 RX:** GPIO 3 (DUT communication)
- **ADC Channels:** GPIO 32-39 (analog inputs)
- **DAC Channels:** GPIO 25-26 (analog outputs)

**Test Interface Controls:**
- **Relay Controls:** GPIO 12-15, 18-19, 21-23 (TBD in carrier board design)
- **Status LED:** GPIO 2 (built-in LED, if available)

**Reserved Pins:**
- **GPIO 0:** BOOT button (strapping pin)
- **GPIO 1:** UART TX (console output)
- **GPIO 3:** UART RX (console input)
- **GPIO 6-11:** Internal flash (DO NOT USE)

---

## Hardware Validation Checklist

Before flashing firmware, verify:

- [ ] **USB cable connected** and power LED on
- [ ] **Serial port detected** by OS (lsusb / Device Manager)
- [ ] **Drivers installed** (if required)
- [ ] **Port permissions granted** (Linux/WSL: dialout group)
- [ ] **No other programs using serial port** (close Arduino IDE, minicom, etc.)
- [ ] **Correct board selected** in platformio.ini (`freenove_esp32_s3_wroom`)

---

## Troubleshooting

### "Serial port not found"
**Fix:**
1. Unplug and replug USB cable
2. Try different USB port on computer
3. Check `lsusb` output (Linux) or Device Manager (Windows)
4. Install/update drivers

### "Permission denied" (Linux)
**Fix:**
```bash
sudo usermod -a -G dialout $USER
# Log out and log back in for group change to take effect
```

### "Failed to connect to ESP32"
**Fix:**
1. Use manual reset procedure (BOOT + RESET buttons)
2. Check USB cable (try a different cable)
3. Reduce upload speed in platformio.ini: `upload_speed = 115200`
4. Try different USB port

### Board enters download mode but won't flash
**Fix:**
1. Check that no other serial monitor is open
2. Close Arduino IDE, minicom, screen, etc.
3. Verify correct board in platformio.ini
4. Try: `pio run -e esp32-Devkit -t upload --upload-port /dev/ttyUSB0`

### Board constantly reboots
**Check:**
1. Power supply (USB port may not provide enough current)
2. Try powered USB hub
3. Check for short circuits on carrier board (if using)

---

## Next Steps

Once hardware is connected and verified:
1. ✅ Hardware connected
2. ✅ Serial port detected
3. ✅ Drivers installed (if needed)
4. **Proceed to Task 5:** Flash firmware with `pio run -e esp32-Devkit -t upload`

---

**Document Version:** 1.0
**Last Updated:** 2026-02-15
**Maintained by:** G3 Tester Client Development Team

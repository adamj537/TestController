# ESP32 Hardware Safety Checklist
## G3 Tester Client - Hardware Validation & Testing

**Project:** G3 Main Board Test Fixture
**Hardware:** Freenove ESP32-S3 WROOM
**Purpose:** Prevent hardware bricking and damage during development
**Date:** 2026-02-15

---

## ⚠️ CRITICAL: Read Before Connecting Hardware

**ESP32 devices are difficult to permanently brick** unless you modify eFuses or cause physical damage. However, following these safety procedures prevents recoverable "soft bricks" and hardware damage.

---

## Pre-Flash Safety Checklist

### Phase 1: Hardware Inspection (BEFORE USB Connection)

**Visual Inspection:**
- [ ] **Board is undamaged** - No cracked components, bent pins, or burn marks
- [ ] **USB connector intact** - No physical damage, pins straight
- [ ] **No foreign objects** - No metal shavings, solder bridges, or debris
- [ ] **Correct board variant** - ESP32-S3 WROOM (or compatible ESP32-DevKit)

**Power Rail Verification:**
- [ ] **3.3V rail present** - Measure with multimeter (should read 3.3V ±5%)
- [ ] **No shorts on power** - Check resistance between 3.3V and GND (should be >100Ω)
- [ ] **No shorts on GPIO** - No GPIO pins shorted to power or ground

**GPIO Protection (CRITICAL for Testing):**
- [ ] **220Ω series resistors** installed on all GPIO outputs (limits short circuit current to 15mA)
- [ ] **No connections to GPIO 6-11** - These are permanently connected to flash, DO NOT USE
- [ ] **Strapping pins protected** - GPIO 0, 2, 12, 15, 45, 46 have ≥10kΩ pull resistors if used

### Phase 2: Software Preparation (BEFORE Flashing)

**Firmware Validation:**
- [ ] **Firmware compiles without errors** - `pio run -e esp32-Devkit` succeeded
- [ ] **No compiler warnings** - Review warnings, fix critical ones
- [ ] **Watchdog timers enabled** - Verify platformio.ini has watchdog flags
- [ ] **Flash size correct** - 8MB for Freenove ESP32-S3 WROOM

**Configuration Verification:**
- [ ] **platformio.ini correct** - Board: `freenove_esp32_s3_wroom`, Framework: `espidf`
- [ ] **Upload speed reasonable** - 460800 (reduce to 115200 if connection issues)
- [ ] **Monitor speed matches** - 115200 for serial output

**Code Review (AI-Generated Code):**
- [ ] **No GPIO 6-11 usage** - CRITICAL: These pins corrupt flash if used
- [ ] **GPIO configured INPUT first** - Never set OUTPUT without checking state
- [ ] **No infinite loops without yield** - All loops have delay or task yield
- [ ] **ADC voltage range safe** - All ADC inputs <3.3V (damage at >3.6V)
- [ ] **DAC output range safe** - DAC outputs configured for 0-3.3V range

### Phase 3: Initial Connection (USB Only, No External Circuits)

**USB Connection:**
- [ ] **USB cable is good quality** - Data-capable USB cable (not charge-only)
- [ ] **Stable power source** - Computer USB port or powered USB hub
- [ ] **Power LED illuminates** - Board powers on when USB connected
- [ ] **Wait 2-3 seconds** - Allow USB enumeration to complete

**Serial Port Detection:**
- [ ] **Serial port detected** - Run `ls /dev/ttyUSB* /dev/ttyACM*` (Linux) or check Device Manager (Windows)
- [ ] **Correct port identified** - Note port name (e.g., /dev/ttyACM0)
- [ ] **Permissions granted** - User in `dialout` group (Linux) or port accessible
- [ ] **No other programs using port** - Close Arduino IDE, minicom, screen, etc.

**Bootloader Communication Test:**
- [ ] **Chip detected by esptool** - Run `pio run -e esp32-Devkit -t nobuild` to test connection
- [ ] **Chip type correct** - Should report ESP32-S3
- [ ] **Flash size detected** - Should report 8MB

### Phase 4: First Flash (Minimal Firmware Recommended)

**Pre-Flash Backup (Optional but Recommended):**
- [ ] **Backup factory firmware** - Run backup command if board is new
  ```bash
  esptool.py --chip esp32s3 --port /dev/ttyACM0 \
      read_flash 0x0 0x800000 factory_backup_$(date +%Y%m%d).bin
  ```

**Flash Procedure:**
- [ ] **Use PlatformIO upload** - `pio run -e esp32-Devkit -t upload` (builds + uploads together)
- [ ] **DO NOT separate build/upload** - Prevents bootloader/partition mismatch
- [ ] **Watch for errors** - Monitor upload progress, check for failures
- [ ] **Verify upload success** - Look for "Leaving... Hard resetting via RTS pin..."

**Post-Flash Verification:**
- [ ] **Board boots** - Power LED stays on, no rapid blinking
- [ ] **Serial output present** - Run `pio device monitor` to see boot messages
- [ ] **No boot loops** - Device boots once and runs (not resetting repeatedly)
- [ ] **Watchdog not triggering** - No "Task watchdog got triggered" messages

### Phase 5: GPIO Testing (Incremental, One at a Time)

**Input Testing (Safe, Passive):**
- [ ] **Configure as INPUT** - Set GPIO mode to INPUT (high-impedance)
- [ ] **Read current state** - Verify input reading works
- [ ] **Apply test signal** - Connect 3.3V or GND via 1kΩ resistor
- [ ] **Verify reading** - Confirm GPIO reads applied state correctly

**Output Testing (With Current Limiting):**
- [ ] **Series resistor installed** - 220Ω resistor on GPIO output
- [ ] **Configure OUTPUT** - Set GPIO mode to OUTPUT
- [ ] **Start LOW** - Always initialize OUTPUT pins to LOW state
- [ ] **Toggle output** - Switch HIGH/LOW, measure voltage with multimeter
- [ ] **Verify voltage levels** - LOW <0.5V, HIGH >2.8V
- [ ] **Check current draw** - Measure current through resistor (<15mA)

**ADC Testing:**
- [ ] **Attenuation configured** - Set appropriate attenuation for voltage range
- [ ] **Input voltage safe** - Test with voltage source <3.3V
- [ ] **Read ADC value** - Verify ADC reading matches applied voltage
- [ ] **No ADC2 with WiFi** - If WiFi enabled, use ADC1 channels only

**DAC Testing:**
- [ ] **Output voltage range** - Configure DAC for 0-3.3V output
- [ ] **Measure output** - Verify DAC output voltage with multimeter
- [ ] **No load or light load** - DAC drives high impedance (>10kΩ) loads only

**UART Testing:**
- [ ] **TX/RX configured correctly** - Verify pin assignments
- [ ] **Baud rate matches** - Both ends configured for same baud rate
- [ ] **Loopback test** - Connect TX to RX via 1kΩ resistor for self-test
- [ ] **External device test** - Connect to DUT or test equipment

### Phase 6: Integration Testing (Full System)

**Power Consumption Check:**
- [ ] **Idle current reasonable** - ESP32-S3 idle ~20-50mA
- [ ] **Active current reasonable** - With WiFi ~120-200mA
- [ ] **No excessive heating** - Board warm but not hot to touch
- [ ] **Power supply adequate** - USB port provides 500mA minimum

**Carrier Board Integration (When Applicable):**
- [ ] **Carrier board inspected** - No shorts, correct voltages
- [ ] **ESP32 seated correctly** - All pins making contact
- [ ] **External power applied** - If carrier has separate power, verify voltage
- [ ] **Relay drivers protected** - Flyback diodes on relay coils

**Test Interface Connections:**
- [ ] **Relay control signals** - Verify GPIO assignments correct
- [ ] **ADC/DAC wiring** - Check voltage dividers, reference voltages
- [ ] **DUT interface protection** - Series resistors, ESD protection present
- [ ] **No load on unused GPIOs** - Unconnected GPIOs left floating or pulled

---

## Emergency Recovery Procedures

### Level 1: Soft Recovery (Press RESET)
**Symptoms:** Code crashed, device unresponsive, watchdog triggered
**Solution:** Press RESET button or power cycle

### Level 2: Manual Bootloader Mode
**Symptoms:** Cannot flash, upload fails
**Procedure:**
1. Hold BOOT button (GPIO0)
2. Press and release RESET button
3. Release BOOT button
4. Run `pio run -e esp32-Devkit -t upload`

### Level 3: Complete Flash Erase
**Symptoms:** Persistent boot loops, corrupted firmware
**Procedure:**
```bash
pio run -e esp32-Devkit -t erase  # Erase entire flash
pio run -e esp32-Devkit -t upload # Re-flash clean firmware
```

### Level 4: Restore from Backup
**Symptoms:** Factory firmware needed, want to undo changes
**Procedure:**
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 \
    write_flash 0x0 factory_backup_20260215.bin
```

### Level 5: Hardware-Level Debugging
**Symptoms:** Device not detected, no power LED, physical damage suspected
**Actions:**
- Measure 3.3V rail with oscilloscope (check for ripple, brownouts)
- Check for short circuits with multimeter
- Try different USB cable, USB port, computer
- If still failed: Replace ESP32 module

---

## GPIO Safety Quick Reference

### ❌ NEVER DO THIS:
- Use GPIO 6-11 (flash interface, WILL corrupt flash)
- Output >40mA per pin (permanent damage)
- Input >3.6V on any pin (permanent damage)
- Short GPIO to ground/VCC without current limiting
- Modify eFuses without understanding consequences (permanent)

### ✅ ALWAYS DO THIS:
- Add 220Ω series resistors on outputs during testing
- Configure as INPUT first, read state, then OUTPUT
- Start OUTPUT pins at LOW state
- Limit ADC inputs to <3.3V
- Enable watchdog timers (CONFIG_BOOTLOADER_WDT_ENABLE)
- Backup flash before risky operations

---

## Testing Progression (Minimal Risk)

**Recommended Order:**
1. ✅ **Compile firmware** (no hardware risk)
2. ✅ **Flash minimal test firmware** (bootloader communication only)
3. ✅ **Verify serial output** (passive monitoring)
4. ✅ **Test GPIO inputs** (high-impedance, safe)
5. ✅ **Test GPIO outputs with resistors** (current-limited)
6. ✅ **Test ADC with known voltages** (voltage-limited)
7. ✅ **Test UART loopback** (self-contained)
8. ✅ **Test with carrier board** (incremental integration)
9. ✅ **Test with DUT** (full system validation)

**Golden Rule:** Test incrementally, one peripheral at a time, with current limiting.

---

## Known-Good Firmware Library

**Maintain test firmwares for validation:**
- `minimal_boot.bin` - Boots and prints "Hello ESP32"
- `gpio_test.bin` - GPIO input/output with serial reporting
- `uart_test.bin` - UART echo loopback
- `adc_test.bin` - ADC continuous reading with serial output
- `full_system.bin` - Complete tester client firmware (this project)

**Use Cases:**
- Hardware validation before flashing development firmware
- Recovery from corrupted flash
- Regression testing after hardware changes
- Carrier board bring-up

---

## Additional Resources

- **Detailed Guide:** See `docs/development_manual/ESP32_Bricking_Prevention_Guide.md`
- **Hardware Setup:** See `HARDWARE_SETUP_GUIDE.md`
- **PlatformIO Troubleshooting:** https://docs.platformio.org/en/latest/platforms/espressif32.html
- **ESP32-S3 Datasheet:** https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- **ESP-IDF Documentation:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/

---

**Version:** 1.0
**Last Updated:** 2026-02-15
**Status:** MANDATORY - Follow before connecting hardware
**Maintained by:** G3 Tester Client Development Team

---

## Sign-Off (Before Proceeding to Hardware)

**I have:**
- [ ] Read this entire safety checklist
- [ ] Read the ESP32 Bricking Prevention Guide
- [ ] Verified firmware compiles without errors
- [ ] Confirmed watchdog timers are enabled in platformio.ini
- [ ] Prepared emergency recovery procedures
- [ ] 220Ω series resistors ready for GPIO testing
- [ ] Multimeter available for voltage/current verification
- [ ] Backup of factory firmware (if applicable)

**Initial Hardware Connection:**
- [ ] USB cable connected
- [ ] Power LED illuminated
- [ ] Serial port detected: `/dev/______` or `COM__`
- [ ] Ready to proceed with controlled flash

**Signature:** ________________  **Date:** ________

---

**Remember:** ESP32 devices are hard to permanently brick. Stay calm, follow recovery procedures, and test incrementally with current limiting.

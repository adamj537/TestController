# ESP32 Emergency Recovery - Quick Reference Card

**Print this page and keep it near your workbench**

---

## 🚨 Don't Panic!

**ESP32 devices are hard to permanently brick.** Most issues are recoverable "soft bricks."

**ROM bootloader is always accessible** - it cannot be corrupted.

---

## Recovery Hierarchy (Try in Order)

### Level 1: Soft Reset ⚡

**Problem:** Code crashed, device frozen, watchdog triggered

**Solution:**
```
Press RESET button on board
OR
Unplug and replug USB cable
```

**Expected:** Device reboots and runs firmware

---

### Level 2: Manual Bootloader Mode 🔧

**Problem:** Cannot flash firmware, upload fails, "Timed out waiting for packet header"

**Procedure:**
1. Hold **BOOT** button (GPIO0)
2. Press and release **RESET** button
3. Release **BOOT** button
4. Device is now in download mode

**Then flash:**
```bash
pio run -e esp32-Devkit -t upload
```

**Expected:** Successful upload, "Hard resetting via RTS pin..."

---

### Level 3: Complete Flash Erase 🧹

**Problem:** Persistent boot loops, corrupted firmware, strange behavior

**Procedure:**
```bash
# Erase entire flash (removes all data)
pio run -e esp32-Devkit -t erase

# Re-flash clean firmware
pio run -e esp32-Devkit -t upload
```

**Expected:** Clean flash, device boots normally

---

### Level 4: Backup Restore 💾

**Problem:** Need factory firmware, want to undo all changes

**Prerequisites:** You created a backup earlier

**Restore from backup:**
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 \
    write_flash 0x0 flash_backup_YYYYMMDD.bin
```

**Expected:** Device restored to backed-up state

---

### Level 5: Hardware Debugging 🔬

**Problem:** Device not detected, no power LED, no USB enumeration

**Check:**
- [ ] Measure 3.3V rail with multimeter (should be 3.30V ±5%)
- [ ] Try different USB cable (must be data-capable, not charge-only)
- [ ] Try different USB port or powered USB hub
- [ ] Check for short circuits (GND to VCC resistance >100Ω)
- [ ] Inspect for physical damage (cracked components, burn marks)

**If still failed:**
- Possible hardware damage (replace ESP32 module)
- Contact hardware support

---

## Quick Commands

### Check if device is detected:
```bash
# Linux
ls /dev/ttyUSB* /dev/ttyACM*

# Identify chip
esptool.py --chip esp32s3 --port /dev/ttyACM0 chip_id
```

### Backup flash (before risky operations):
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 \
    read_flash 0x0 0x800000 backup_$(date +%Y%m%d_%H%M%S).bin
```

### View serial output:
```bash
pio device monitor -b 115200
```

### Reduce upload speed (if connection unstable):
Edit `platformio.ini`:
```ini
upload_speed = 115200  # Reduce from 460800
```

---

## Common Error Messages

### "Serial port not found"
**Fix:** Check USB cable, try different port, verify permissions
```bash
sudo usermod -a -G dialout $USER  # Linux
```

### "Failed to connect to ESP32"
**Fix:** Use manual bootloader mode (Level 2)

### "Timed out waiting for packet header"
**Fix:** Board not in download mode, use BOOT button

### "Task watchdog got triggered"
**Fix:** Infinite loop in code, add delays or task yields

### "Brownout detector was triggered"
**Fix:** Power supply insufficient, use powered USB hub

---

## ⚠️ DO NOT DO (Permanent Damage Risk)

- ❌ Modify eFuses (especially `USB_PHY_SEL` on ESP32-S3)
- ❌ Use GPIO 6-11 (flash interface, corrupts flash)
- ❌ Apply >3.6V to any pin (permanent damage)
- ❌ Draw >40mA from GPIO pin (permanent damage)
- ❌ Short GPIO to GND/VCC without current limiting

---

## ✅ Safe Practices

- ✅ Always use 220Ω series resistors on GPIO outputs during testing
- ✅ Configure GPIO as INPUT first, then OUTPUT
- ✅ Enable watchdog timers (CONFIG_BOOTLOADER_WDT_ENABLE)
- ✅ Backup flash before risky operations
- ✅ Use PlatformIO build+upload together (not separately)
- ✅ Test incrementally (one peripheral at a time)

---

## Emergency Contact

**Project:** G3 Main Board Test Fixture - Tester Client
**Documentation:** `embedded/tester-client/SAFETY_CHECKLIST.md`
**Detailed Guide:** `docs/development_manual/ESP32_Bricking_Prevention_Guide.md`

**ESP32 Resources:**
- PlatformIO: https://docs.platformio.org/en/latest/platforms/espressif32.html
- Esptool: https://docs.espressif.com/projects/esptool/
- ESP-IDF: https://docs.espressif.com/projects/esp-idf/

---

**Version:** 1.0 | **Date:** 2026-02-15
**Keep this card near your workbench for quick reference during emergencies**

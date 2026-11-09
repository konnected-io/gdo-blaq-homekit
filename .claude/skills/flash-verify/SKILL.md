---
name: flash-verify
description: Build the firmware, flash it to a connected GDO blaQ (ESP32-S3) over USB serial, and check the boot log. Use after changing firmware code to verify it works on real hardware, since the repo has no automated tests.
---

Verify a firmware change on a connected device.

1. **Prereqs.** Confirm `idf.py` is on PATH (otherwise ask the user to `. $IDF_PATH/export.sh`). If `components/gdolib` or `esp-homekit-sdk` is empty, run `git submodule update --init --recursive`.

2. **Build.** Run `idf.py build`. On failure, report the first real compiler error with file:line and stop.

3. **Find the port.** `ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null`. If $ARGUMENTS names a port, use it. If there are none or several, ask the user which port to use. Don't guess.

4. **Flash.** `idf.py -p <PORT> flash`.

5. **Capture the boot log.** `idf.py monitor` is interactive, so read the serial output non-interactively for about 30 seconds instead. For example:
   `timeout 30 python -m serial.tools.miniterm --raw <PORT> 115200 > /tmp/blaq-boot.log` (or `idf.py -p <PORT> monitor --no-reset` with a timeout), then read the log.

6. **Check for:**
   - Crashes: `Guru Meditation`, `abort()`, `assert failed`, `Backtrace:`, watchdog resets, or boot loops (repeated `rst:` lines). If there's a backtrace, decode it with `xtensa-esp32s3-elf-addr2line -pfiaC -e build/gdo-blaq-homekit.elf <addrs>`.
   - Wi-Fi: it got an IP. If instead it started the SoftAP `konnected-blaq-hk`, the device has no saved credentials. That's expected on a fresh device, not a failure.
   - HomeKit: HAP started and the accessory was advertised.
   - GDO: a protocol was detected and the gdolib sync event arrived. Note any `synced=false` retries.

7. **Report.** Give a short pass/fail summary for each check, quoting the relevant log lines. Note anything that needs a human to test physically, such as opening the door, light/lock behavior or pairing from the Home app.

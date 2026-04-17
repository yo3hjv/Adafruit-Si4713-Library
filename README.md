# Adafruit-Si4713-Library 

Here are the original Adafruit SI4713 library files modified to add other functions from the chipset datasheet andfix some initialisation errors.
A test .ino file is provided for both old and new commands.
To use it, put .cpp and .h files in the same folder as the .ino main code file.
You will still need to install Adafruit BusIO library!

This is the Adafruit FM Transmitter with RDS/RBDS Breakout - Si4713 library with some important add-ons and fixes.
For a detailed description of what was added, please read CHANGELOG.md

Tested and works great with other SI4713 boards.

# Changelog — Adafruit Si4713 Library (local fork)

All changes relative to the original Adafruit Si4713 library
([adafruit/Adafruit-Si4713-Library](https://github.com/adafruit/Adafruit-Si4713-Library)).

---

## [1.1.0] — 2026-04-18

### Fixed

#### `Adafruit_Si4713.cpp` — `begin()`
- **CRITICAL — second root cause of intermittent initialization failures.**
  `i2c_dev->begin()` (which performs an I2C address scan) was called **before**
  `reset()`. If the chip was in an undefined power-on state it would not ACK
  its I2C address, `begin()` returned `false`, and the MCU required multiple
  resets before the chip was caught in a good state by chance.
  **Fix:** `reset()` is now called first, giving the chip time to boot before
  the I2C scan is attempted.

#### `Adafruit_Si4713.cpp` — `reset()`
- **CRITICAL — root cause of intermittent initialization failures.**
  After de-asserting NRST (`digitalWrite(_rst, HIGH)`), the function returned
  immediately with no delay. `powerUp()` was then called before the Si4713
  had completed its internal boot sequence, causing random I2C command failures
  on ESP32 and other fast MCUs.
  **Fix:** added `delay(100)` after the final NRST HIGH to satisfy the
  datasheet power-up timing requirement (Si4712-13-B30-1, §8).

#### `Adafruit_Si4713.cpp` — `sendCommand()`
- The CTS polling loop had no timeout, blocking the MCU indefinitely if the
  chip failed to assert CTS (e.g. after a failed reset or I2C fault).
  **Fix:** added a 5-second timeout; on expiry a diagnostic message is printed
  to `Serial` and the function returns gracefully.

#### `Adafruit_Si4713.cpp` — `getRev()`
- Chip revision information (part number, firmware, patch, component, chip rev)
  was only printed when the `SI4713_CMD_DEBUG` macro was defined at compile
  time, making it impossible to verify the detected chip during normal startup.
  **Fix:** revision info is now always printed to `Serial` unconditionally.
- The original debug block printed the firmware word twice (once in the
  "Part #" line and once in "Firmware") and never printed the component
  version (`cmp`). Both issues are corrected.
- Per the Si4713 datasheet, `FWMAJOR`, `FWMINOR`, `CMPMAJOR` and `CMPMINOR`
  are **ASCII characters** (same encoding as `CHIPREV`). The previous
  implementation printed them with `Serial.print(..., HEX)`, producing
  incorrect output such as `Firmware : 32.30` instead of `Firmware : 2.0`.
  **Fix:** all four fields are now displayed with `Serial.write()`.

#### `Adafruit_Si4713.cpp` — `powerUp()`
- Corrected a misleading inline comment: `TX_ACOMP_ENABLE = 0x0` was labelled
  *"turn on limiter and AGC"* when it actually **disables** the audio
  compander. Comment updated to reflect the correct behaviour and documents the
  alternative values (`0x02` = limiter only, `0x03` = limiter + AGC).

#### `Adafruit_Si4713.h` — include guard
- Added `#pragma once` to prevent multiple-inclusion issues when the header is
  used in a multi-file sketch alongside other Adafruit libraries.

#### `Adafruit_Si4713.h` — debug macro name mismatch
- The commented-out debug macro was named `SI471X_CMD_DEBUG`, while the `.cpp`
  implementation checked for `SI4713_CMD_DEBUG`. Uncommenting the header macro
  therefore had no effect.
  **Fix:** renamed to `SI4713_CMD_DEBUG` to match the preprocessor guards in
  the implementation file.

### Added

#### `Adafruit_Si4713.cpp` / `.h` — `powerDown()`
- Implements the `POWER_DOWN` command (0x11). Puts the chip in low-power state
  without requiring a hardware reset. Call `begin()` to restart.

#### `Adafruit_Si4713.cpp` / `.h` — `powerUpDigital(sampleRate)`
- Alternative to `powerUp()` for I2S digital audio input (POWER_UP ARG2=0xD0).
  Configures `SI4713_PROP_DIGITAL_INPUT_FORMAT` (standard I2S, 16-bit, stereo)
  and `SI4713_PROP_DIGITAL_INPUT_SAMPLE_RATE` (default 44100 Hz).

#### `Adafruit_Si4713.cpp` / `.h` — `setAudioCompander(mode, threshold, attack, release, gain)`
- Convenience wrapper to configure the audio dynamic range compander in one
  call. Sets `TX_ACOMP_THRESHOLD`, `TX_ATTACK_TIME`, `TX_RELEASE_TIME`,
  `TX_ACOMP_GAIN`, then `TX_ACOMP_ENABLE` (mode last, as required by datasheet).
  mode: 0=off, 0x02=limiter only, 0x03=limiter+AGC.

#### `Adafruit_Si4713.cpp` / `.h` — `setASQThresholds(levelLow, durLow, levelHigh, durHigh)`
- Configures Audio Signal Quality thresholds for silence and overdrive detection.
  Enables both ASQ interrupts (`TX_ASQ_INTERRUPT_SOURCE = 0x03`) and sets the
  four threshold/duration properties. Results are readable via `readASQ()`.

#### `Adafruit_Si4713.cpp` / `.h` — `scanNoise(startFreq, endFreq, step)`
- Scans a range of FM frequencies using `TX_TUNE_MEASURE` + `TX_TUNE_STATUS`
  and returns the frequency (in 10 kHz units) with the lowest noise floor.
  Default step is 10 (100 kHz channel spacing). Useful for automatic channel
  selection before transmission.

#### `Adafruit_Si4713.cpp` / `.h` — `setRDSpi(pi)`
- Standalone setter for the RDS Programme Identifier (PI) code
  (`SI4713_PROP_TX_RDS_PI`). Allows changing PI independently of `beginRDS()`.

#### `Adafruit_Si4713.h` — ASQ define name aliases
- The original header defined `SI4713_PROP_TX_AQS_LEVEL_HIGH` and
  `SI4713_PROP_TX_AQS_DURATION_HIGH` with a typo (`AQS` instead of `ASQ`).
  Added corrected-name aliases `SI4713_PROP_TX_ASQ_LEVEL_HIGH` and
  `SI4713_PROP_TX_ASQ_DURATION_HIGH` to allow consistent naming in user code.

---

## [1.0.0] — original Adafruit release

Base library as published by Adafruit Industries (Limor Fried / Ladyada).
BSD licence.



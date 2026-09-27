# Milestone 2: peripheral bring-up

Firmware version 0.2.0. Production/development builds compile against the pinned
ESP-IDF v6.1 without Arduino or new component dependencies. The user verified
milestone 1 on-device. For milestone 2, the user reports the OLED, encoder/light
sensing and 5% audio test work; these are not agent-observed tests. RTC retention,
battery accuracy and the remaining fault-recovery checks are still unverified.

## What is implemented

| Area | Behavior |
| --- | --- |
| I2C | GPIO8/9 at 100 kHz, external module pull-ups; address discovery and serialized transfers |
| Display | SH1107 reset on GPIO10, 64x128 native buffer rotated to 128x64 landscape; clock and status pages |
| RTC | Read/validate PCF8523, initialize system UTC from valid RTC, explicit manual setting, enable battery switchover when setting |
| Controls | GPIO11/12 and seesaw GPIO24 switch with 30 ms debounce; encoder position polling and rollover handling |
| Audio | Dedicated task; explicit 440 Hz test, two seconds, 1% digital peak and 100 ms envelope, stereo mono mix |
| Battery | ADC1 channel 0 / GPIO1, 12 dB attenuation, 16-sample average, eFuse calibration if available, nominal divider factor 2 |
| Light | BH1750 continuous high-resolution mode; lux reading and optional coarse ambient dimming |
| Diagnostics | Local serial commands, time/peripheral status, memory and heartbeat logs; no radio or web service |

Exact build, flash, monitor and manual test instructions are in the root README.
The existing `build/m1` and `build/m1-dev` names are intentionally retained; they
build the current source, not a frozen milestone 1 image. Use commit `03fb405` in
a separate checkout if a milestone 1 rebuild is needed. No settings/NVS partition
layout change is required for milestone 2.

## Serial commands and physical controls

| Input | Result |
| --- | --- |
| `help` | Command list |
| `clock_status` | UTC validity/source, RTC/OLED/encoder/audio status, ADC, battery mV, lux |
| `i2c_scan` | Probe non-reserved 7-bit addresses; stop early on bus timeout/error |
| `clock_set YYYY-MM-DDTHH:MM:SSZ` | Validate and set actual UTC; attempt RTC write and report failures |
| `audio_test [1\|5]` | Start bounded tone at 1% (default) or 5% digital peak if ready and idle |
| `brightness 1` through `brightness 255` | OLED contrast; no persistence yet |
| `ambient on` / `ambient off` | Enable/disable light-based contrast ceiling; no persistence yet |
| Encoder rotation | Adjust contrast, clockwise positive for the preassembled 5880 |
| Encoder push / button 1 | Alternate OLED clock/status page |
| Button 2 | Log status only |

No physical input starts sound automatically. Boot is silent. In development,
serial and OLED identify the development build; simulated NFC events are still
pending milestone 3. This console is a locally attached diagnostic interface,
not an authenticated network API. Before integrating the alarm state machine,
route diagnostic intents through it and enforce the ringing restrictions on
time changes, audio tests and any future reset/enrollment operations.

## Fault handling and boundaries

- RTC invalid BCD/calendar, oscillator-stop or STOP/12-hour flags prevent using
  its time. Disabled/invalid battery backup modes request manual setting.
  Battery-low or disabled-monitor status is visible. Manual setting selects
  24-hour mode and standard battery switchover, retaining oscillator capacitance.
- A successful system time set remains usable in RAM if RTC writes fail. A
  partial RTC write can leave it stopped: report the fault, then retry
  `clock_set` after fixing the connection. No fabricated build-time fallback.
  Subsequent RTC reads check validity; battery retention and accuracy still
  require the power-cycle/multimeter checks in the README.
- System time is initialized once from valid RTC or explicitly set. A later
  RTC communication failure does not erase the running system time; status shows
  the fault while the MCU clock provides holdover. Periodic network sync and
  timezone/DST handling are not implemented in this milestone.
- Every I2C transfer has a 20 ms driver timeout and a 30 ms mutex acquisition
  limit. Failed devices retry after five seconds. Device transfers and command
  handling share one peripheral task, so faults may slow its UI/controls polling.
  The audio task is independent. The future alarm task must also be independent.
- OLED updates send one 64-byte page per task iteration rather than monopolizing
  the bus for a complete frame. Initialization has bounded reset/startup delays.
  A shared bus short can make all I2C devices unavailable; firmware cannot isolate
  the electrical fault. Direct buttons, UART and I2S can still operate.
- Audio has no automatic amplifier/speaker presence detection. `READY/UNVERIFIED`
  means the ESP32 I2S driver initialized. Requests while busy are rejected. DMA
  writes have a 100 ms timeout; silence is flushed before clocks stop. A driver
  failure disables further tests until reboot. This is not an amplifier SD control.
  The optional `audio_test 5` diagnostic raises digital amplitude fivefold without
  changing the duration, envelope, wiring or hardware gain. Invalid levels are
  rejected by the console and driver. Host tests verify the generated PCM, not
  DMA transfers or electrical signals. The configured Philips I2S format uses
  16-bit stereo slots at 16 kHz: nominal LRC 16 kHz, BCLK 512 kHz. This format is
  supported by the [MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf).
- ADC calibration is not a calibrated battery gauge. `-1` denotes an unavailable
  measurement; raw counts remain available when eFuse calibration is unavailable.
  No battery charge limiting, low-battery shutdown or runtime estimate is provided.
- Ambient mode uses contrast ceilings 8/32/79/160 at lux bands below 5/50/200/above,
  bounded by manual brightness. Missing light data restores manual contrast.
  This is a bring-up policy; smoothing/hysteresis can follow physical testing.

## Protocol references checked

The native IDF APIs were checked against the installed v6.1 headers and compiled.
Device register behavior was checked against manufacturer references:

- [Adafruit SH1107 source](https://github.com/adafruit/Adafruit_SH110x/blob/master/Adafruit_SH1107.cpp)
  and [SH110X page transfers](https://github.com/adafruit/Adafruit_SH110x/blob/master/Adafruit_SH110X.cpp).
  Adapted initialization/page addressing is covered by `THIRD_PARTY_NOTICES.md`.
- [NXP PCF8523 datasheet](https://www.nxp.com/docs/en/data-sheet/PCF8523.pdf),
  register map, oscillator validity and battery-management modes; compared with
  [Adafruit RTClib](https://github.com/adafruit/RTClib/blob/master/src/RTC_PCF8523.cpp).
- [Adafruit encoder guide](https://learn.adafruit.com/adafruit-i2c-qt-rotary-encoder/arduino)
  confirms firmware product ID 4991, switch on seesaw GPIO24 and reversed direction
  on preassembled product 5880. Native register reads use STOP, a processing delay
  and a separate read, as in the [seesaw driver](https://github.com/adafruit/Adafruit_Seesaw).
- [Adafruit BH1750 implementation](https://github.com/adafruit/Adafruit_CircuitPython_BH1750/blob/main/adafruit_bh1750.py)
  for measurement mode, conversion delay and default scaling.

Reference drivers are not build dependencies; no online downloads are needed to
build these modules. No hardware files or pin assignments were changed.

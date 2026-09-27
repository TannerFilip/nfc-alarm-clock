# NFC-dismiss alarm clock firmware

Milestone 3: portable alarm core and development-only simulated NFC. **Not yet a
production-operational alarm clock.**
The user confirmed milestone 1 works on-device. Milestone 2 has not been flashed
or physically verified by the agent. The user reports the OLED, encoder/light
sensing and 5% audio test work. The PN7160 board is now connected, but its straps,
VEN/IRQ behavior and NCI communication remain unverified. Milestone 3 builds and
host tests pass. The user reports its development alarm sounded and was dismissed
through the enrolled simulated-tag path on the device.

## Build and flash

Pinned SDK: **ESP-IDF v6.1**, commit
`fff9895c82d744c7237be8847347bdd1b07c6643`. No Arduino or external components.
Use Espressif Installation Manager to install v6.1 for ESP32-S3, or the pinned
`.devcontainer` image. In VS Code select that installation with the Espressif
extension, then open an ESP-IDF terminal. Do not use the old root `sdkconfig`:
the original starter targeted ESP32 / 2 MB. Use the isolated build below.

On this workstation the EIM environment can be activated with:

```bash
source /home/tanner/.espressif/tools/activate_idf_v6.1.sh
idf.py --version
```

From the repository root, production profile (simulation disabled):

```bash
idf.py -B build/m1 -D SDKCONFIG=build/m1/sdkconfig build
idf.py -B build/m1 -D SDKCONFIG=build/m1/sdkconfig -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the USB-to-UART bridge's port; use the DevKit's UART
USB connector. Monitor baud is 115200. Exit monitor with Ctrl+]. If automatic
download fails, hold BOOT, tap RESET, release BOOT, and retry flash. These
commands write bootloader, partition table and application; no erase-flash is
required. Flash only the specified N32R16V module. Do not burn eFuses.

Follow the supplied [hardware power instructions](hardware/breadboard-prototype/README.md):
**disconnect U3 OUT from U1 5V before attaching programming USB**. Remove USB
before reconnecting U3. The boost supply and programming USB must not be connected
to U1 simultaneously under this wiring guide.

Explicit development profile, in a separate configuration/build directory:

```bash
idf.py -B build/m1-dev -D SDKCONFIG=build/m1-dev/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.development' build
idf.py -B build/m1-dev -D SDKCONFIG=build/m1-dev/sdkconfig -p /dev/ttyUSB0 flash monitor
```

This prints the development/simulation banner on serial and OLED and enables the
RAM-only Milestone 3 alarm demo. The `build/m1` and `build/m1-dev` directory names
are retained for VS Code compatibility; they build the current milestone 3 source.
Never ship the development profile. To use the extension's
Build/Flash buttons instead, the checked-in workspace settings currently select the
development profile: `idf.buildPath` is `${workspaceFolder}/build/m1-dev`, and
`idf.cmakeCompilerArgs` supplies both `-DSDKCONFIG=build/m1-dev/sdkconfig` and
`-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.development`. Select
the UART port. The CLI commands above are the reference workflow.
These development build settings are already supplied in `.vscode/settings.json`.

## Milestone 3 development acceptance

Use only the development profile for this demo. Its schedule, timezone, enrolled
tag and journal are RAM-only and disappear on reset; the production profile does
not contain these mutation/simulation commands. Set valid UTC first, then run:

```text
tag_enroll 01020304
alarm_zone America/Los_Angeles
alarm_set 07:00 0111110
alarm_status
alarm_trigger
```

The weekday mask is Sunday through Saturday, so `0111110` means Monday-Friday.
Use an alarm time one or two minutes ahead to test normal scheduled firing.
`alarm_trigger` is a development shortcut but enters the same ringing state.
Expect a repeating 440 Hz tone that ramps from 1% to 5% digital peak over 30
seconds and an `ALARM RINGING` OLED warning.

While ringing, buttons and the encoder may change diagnostic pages/brightness but
must not dismiss the alarm. `clock_set`, `audio_test`, `alarm_set`, `alarm_zone`,
tag enrollment and another trigger must be rejected or have no bypass effect.
An unknown simulated tag must leave it ringing:

```text
nfc_sim AABBCCDD
alarm_status
```

The enrolled tag must take the identical authorization path and stop the alarm
only after its dismissal is accepted by the journal:

```text
nfc_sim 01020304
alarm_status
```

Flash the production profile afterward and confirm `help` has no `nfc_sim`,
`tag_enroll`, `alarm_set`, `alarm_zone`, or `alarm_trigger` command and no simulator
OLED banner. Full steps and policy boundaries are in [Milestone 3](docs/milestone-3.md).

## Remaining Milestone 2 manual acceptance

Follow the power/flash instructions above. Open the UART console at 115200 baud.
Type `help` at `clock>` to list commands. Press Enter if periodic status logging
has displaced the prompt. Each command queues work; the log reports its result.

1. Check the current milestone **3 / 0.3.0** banner and `memory=OK` (33554432 bytes flash,
   16777216 bytes PSRAM). Production must report simulation disabled; development
   must show a development/simulation warning on serial and OLED.
2. Run `i2c_scan`. Expect ACKs at **0x23, 0x36, 0x3C, 0x68** for the installed
   modules. An ACK does not prove identity; missing devices are reported separately.
   An attached PN7160 may ACK at 0x28-0x2B if already enabled, but an ACK does not
   prove NCI operation. No NFC driver is started and GPIO15/16 remain untouched.
3. Run `clock_status`. Check the OLED is legible in landscape, with no clipped or
   shifted lines. An unset RTC should show an invalid-time message, not a guessed
   clock. The default display uses UTC; timezone support comes with scheduling.
4. Set the actual current UTC time using the format below (replace the example):

   ```text
   clock_set 2026-09-19T22:00:00Z
   clock_status
   ```

   Expect a running OLED clock and RTC status `OK` (or an explicit battery fault).
   Dates outside 2000–2099 and malformed/impossible dates are rejected. Without an
   RTC, manual time works in RAM and is explicitly reported as not written to RTC.
   With its backup coin cell installed, remove MCU/main power for a minute, then
   reconnect using the wiring guide's power procedure. Time should have advanced
   and source should be `RTC`. If the oscillator was stopped, allow two seconds
   and set UTC again if its validity flag persists.
5. Rotate the encoder: clockwise should increase contrast and counterclockwise
   decrease it. Press the encoder or button 1 to alternate clock/status pages.
   Button 2 prints status only. Each press/release should appear once in the log;
   none of these controls dismiss an alarm or start sound.
6. Run `brightness 20`, then `brightness 120`. Run `ambient on`, shade/uncover the
   BH1750 and check contrast follows coarse light bands up to the manual ceiling.
   `ambient off` restores manual contrast. These settings are RAM-only and reset
   on reboot. Missing light readings fall back to manual brightness.
7. Compare `battery_mV` with a multimeter on the battery terminal. ADC calibration
   is from the ESP32 eFuses, with nominal divider factor 2; resistor tolerances and
   board accuracy still need checking. `-1` means unavailable. If calibration is
   unavailable, only raw ADC counts are reported. No percentage is estimated.
8. With the amplifier powered from LOAD OUT and the speaker wired as documented,
   explicitly run `audio_test`. Expect one **two-second 440 Hz tone**, 1% digital
   peak with 100 ms fade-in/out, then silence. Try another request during playback:
   it should be rejected as busy, not extend the tone. Try `clock_status` and
   controls during playback. The test drives both I2S channels equally; digital
   amplitude is not a calibrated acoustic level. SD is not controlled by firmware.
   If the 1% tone is inaudible after checking supply/SD voltage, speaker resistance
   and wiring, run `audio_test 5` for a two-second test at 5% digital peak. Only
   levels `1` and `5` are accepted; no argument retains the original quiet 1% test.
   Wait for completion before another test. The extra I2S log describes configured
   pins/clock rates, not measurements at the amplifier.
9. With power off, disconnect one peripheral and reboot. Confirm a clear missing
   status and continued operation of unrelated controls/console/audio. Restore
   wiring with power off and reboot; verify recovery. The drivers also retry I2C
   faults at five-second intervals, but do not hot-plug an energized prototype.

For additional expected results, reference protocols, and limitations, see
[the milestone 2 handoff](docs/milestone-2.md).

## Automated checks

```bash
python3 tools/check_build.py build/m1
python3 tools/check_build.py build/m1-dev --development
cmake -S tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

A memory mismatch logs a fault. Missing PSRAM remains allowed for diagnostics,
but is a failed hardware acceptance check. Compilation and host tests do not
validate physical wiring, OLED orientation, audio output or RTC battery retention.

See [architecture and policies](docs/architecture.md),
[hardware evidence](docs/hardware.md), [validation](docs/validation.md), and
[future features](docs/backlog.md).

# Validation record

Milestone 1, 2026-09-19, ESP-IDF v6.1 commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa toolchain supplied by EIM.

| Check | Result |
| --- | --- |
| Production clean isolated build | PASS, application 181600 bytes |
| Development clean isolated build | PASS, application 181696 bytes |
| IDF bootloader and partition size checks | PASS, factory partition 4 MiB |
| Generated ESP32-S3, 32 MB OPI/DTR 80 MHz, octal PSRAM 80 MHz configuration | PASS both profiles |
| Development/simulation settings | OFF production; ON development |
| Production checker given development build | Correctly rejected |
| Wiring CSV vs supplied exported netlist | PASS, 73 terminals agree |
| Supplied hardware file hashes | Unchanged from inspection through final check |
| `git diff --check` | PASS |

Builds emit upstream IDF CMake component dependency/include visibility warnings;
neither application compilation nor linking failed. Initial CMake version-check
failure was fixed before the successful builds. Build logs are preserved in
`build/m1/build-verification.log` and `build/m1-dev/build-verification.log`.

Reproduce configuration/artifact checks after building with the README commands:

```bash
python3 tools/check_build.py build/m1
python3 tools/check_build.py build/m1-dev --development
```

Application SHA-256 values (exact current build artifacts, not a promise of
byte-identical binaries across environment/path changes):

```text
production  2a92c49a673a7d1f9fa18c1e5707335a561b94fa0190e794442c101a93a15846
development 7daf101cfbae252ab4a4ac689be184ad2e86bd6c0886a83352e0129bd284687e
```

Milestone 1 hardware execution: the **user subsequently reported that it works on
the device** after commit `03fb405`. No agent-observed serial log or item-by-item
acceptance result was supplied. This does not verify milestone 2 peripherals.

## Milestone 2, firmware 0.2.0

Implemented peripheral drivers and serial integration without changing the SDK,
partition layout, wiring or hardware files. Build directories retain their
existing names for VS Code compatibility.

| Check | Result |
| --- | --- |
| First driver increment | Compiled I2C, RTC, sensing, controls and portable helpers |
| Integrated production build (`build/m1`) | PASS, application 317488 bytes |
| Integrated development build (`build/m1-dev`) | PASS, application 317600 bytes |
| IDF bootloader/application partition checks | PASS, application fits 4 MiB partition |
| Memory configuration and profile/artifact checker | PASS both profiles |
| Production checker given development artifacts | Correctly rejected |
| Host CTest suite | PASS, 1 executable containing calendar/control cases |
| Calendar coverage | Every valid date in 2000–2099 round-trips through RTC BCD |
| UTC validation | Known epoch values; reject invalid leap dates, days, ranges, syntax and trailing data |
| RTC validity | Reject OS, STOP, 12-hour format, invalid BCD and impossible dates |
| Controls | Bounce rejection, one event per stable transition and modulo encoder rollover |
| Whitespace and hardware diff checks | PASS; no hardware changes |

The first integrated compile found a missing C++ iterator include and incomplete
console command initializers; both were fixed and the final builds passed with
no application compiler warnings. IDF can still emit its upstream CMake component
visibility warnings on reconfiguration. Logs are retained locally as
`build/m1/milestone-2-build.log` and `build/m1-dev/milestone-2-build.log`.

Initial milestone 2 application SHA-256 values (before the OLED retry fix below):

```text
production  3de171879aa6c0906f972ca82a95b2e810e6b1c452f0572b29b7e2d52b4866db
development 5db2e3dec8061dd4f7ef880cc7ada68f91ca9157d9d5212d3e865f77d8d63e43
```

**Milestone 2 hardware execution: not performed.** No firmware flash, serial
monitor, physical peripheral/audio test or battery-runtime measurement was
performed by the agent. The portable tests do not mock or validate IDF drivers.
RTC retention, display layout, I2C fault recovery, audio amplitude and calibrated
voltage accuracy all require the README's on-device tests. No alarm scheduler
tests exist yet because scheduling is not implemented; those are a required
milestone 3 gate, and persistent settings validation tests are a milestone 4 gate.

### User bring-up report and OLED retry correction (2026-09-21)

The user reports milestone 2 flashed successfully, with no OLED image or speaker
sound; other features seemed to work. Supplied serial output shows ACKs at 0x23,
0x36 and 0x68, a verified seesaw product 4991 / hardware ID 0x55, RTC invalid time,
OLED missing/I/O error, and audio driver initialized but physically unverified.
Battery readout was 880 mV; this is not validated battery voltage. The first lux
sample was unavailable while measurement startup was pending. No audio-test
start/completion/error log or later light reading was supplied.

Inspected the installed IDF v6.1 `gpio.c`: configuring an already-reserved output
prints `conflict found for GPIO`. The OLED retry path was reconfiguring its own
GPIO10 every five seconds. Changed it to configure that pin once, retaining the
reset pulse on subsequent device-init attempts. Added an explicit probe error.
This addresses the misleading warning; it does not establish the reason for the
missing OLED ACK or verify physical display/audio output. No pin assignments or
hardware files were changed by this correction.

Correction validation: production and development builds and their generated
configuration/artifact checks pass. Logs: `build/m1/oled-retry-fix-build.log` and
`build/m1-dev/oled-retry-fix-build.log`. Not flashed by the agent. On-device check:
with OLED still absent, wait through two five-second retries; expect explicit
probe failures without repeated GPIO10 configuration-conflict warnings.

### Audio troubleshooting and optional 5% test (2026-09-26)

User reports the OLED now works. Their log shows a completed 1% audio test without
driver errors, OLED/encoder OK and lux 104.2. RTC remains invalid and battery ADC
reports 94 mV; neither RTC time nor battery measurement is thereby validated.
User measured amplifier VIN around 4.3 V, SD around 416 mV, speaker resistance
4 ohms and good wiring continuity, but still hears no sound. These observations
do not establish I2S signal integrity or acoustic output.

Added `audio_test 5`; no argument remains 1%. Only 1% and 5% levels are accepted,
with unchanged two-second duration, 100 ms fades, both channels identical, and
busy rejection. No pin assignment, amplifier gain, SD or hardware changes.
The configured I2S mode matches the MAX98357A-supported 16-bit stereo Philips
format; an added log gives expected pin/clock settings, explicitly not measured.

Host tests pass for command-level validation, nonzero PCM, 440 Hz frequency,
peak limits, endpoint silence, fades and fivefold amplitude. Existing calendar/
control tests also pass (2/2 CTest executables). This does not verify the DMA
output or the amplifier. No agent flashing or physical audio test performed.

Production and development firmware builds pass, as do both generated
configuration/artifact checks. Build logs: `build/m1/audio-level-build.log` and
`build/m1-dev/audio-level-build.log`.

Manual check: use the README's build/flash commands and power procedure
(disconnect U3 OUT from U1 5V before programming USB). Keep amplifier VIN on
charger LOAD OUT with common ground. Run `audio_test 5` once; expect the 5% start
message, a two-second tone and completion. If silent, retain the log for further
I2S/hardware diagnosis; do not infer a failed amplifier from software success.

### User audio confirmation (2026-09-27)

The user reports that audio works: the original 1% test was too quiet to hear,
while the 5% diagnostic tone is audible. This is user-reported physical validation,
not an agent-observed test or a calibrated acoustic measurement. It confirms the
prototype can produce the bounded test tone at 5% with the current wiring and
firmware; it does not validate RTC retention, battery-voltage accuracy, peripheral
fault recovery or alarm behavior, which is not implemented yet.

Final pre-commit validation rebuilt both pinned profiles and reran all checks:

| Check | Result |
| --- | --- |
| Production build/checker | PASS, 318128-byte application |
| Development build/checker | PASS, 318256-byte application |
| Host CTest suite | PASS, 2/2 executables |
| `git diff --check` | PASS |

```text
production  7b490052962865de0330ecfb19ae465c12f31f212641cf556b7e2341ff16fd4c
development 7deead73616cf244f991046288f9b7b8c2f1c80b5ec04aa45ee0002a164e41b6
```

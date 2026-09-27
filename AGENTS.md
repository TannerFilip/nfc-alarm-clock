# Agent instructions

## Scope and current status

Build a standalone NFC-dismiss alarm clock incrementally in C++ using ESP-IDF.
Keep the workflow compatible with VS Code's Espressif extension. Read this file,
`README.md`, `docs/architecture.md`, `docs/hardware.md`, `docs/validation.md`, and
the relevant hardware documentation before making changes. Inspect `git status`
and preserve existing user edits, including IDE configuration.
Read `docs/backlog.md` for requested future features: battery charge limiting,
occasional Wi-Fi time sync and a deliberate physical-action backup dismissal.
The backup mechanism is undecided; update the dismissal policy explicitly when
it is designed, rather than treating ordinary controls as an implicit override.

Milestone 3 adds the portable alarm core, weekday/DST scheduling, journal failure
semantics, a dedicated alarm-owner task, a controlled audio ramp and a development-
only simulated NFC path. Device-side Milestone 3 configuration and its journal are
deliberately RAM-only until Milestone 4 adds validated NVS and the protected web UI;
production exposes no alarm/tag mutation or simulated-tag commands yet.
Milestone 2 added peripheral bring-up, a serial console and host-tested calendar/control helpers.
The user now reports OLED, encoder/light sensing and audio working; the original
1% tone was simply too quiet, and the 5% diagnostic was audible. These are
user-reported checks, not agent-observed measurements. RTC retention, battery
voltage accuracy and the remaining manual fault checks are still outstanding.
Milestone 1 implemented
boot/memory diagnostics only. The user confirmed that it
works on the physical device after commit `03fb405`. This is user-reported
validation, not an agent-observed serial log or confirmation of every manual
acceptance item. The user reports that the PN7160 board is now connected, but its
exact I2C variant, 3.3 V selection, address straps, IRQ/VEN behavior and NCI
communication have not been verified from a log or by the agent. The user reports
the development alarm sounded and stopped through the enrolled simulated-tag path;
this is user-reported on-device validation, not a physical PN7160 read. Milestone 3
leaves GPIO15/16 untouched; an I2C ACK is presence evidence only. See
`docs/milestone-3.md` for status and manual acceptance. The web UI, NVS persistence
and physical PN7160 NCI driver are not implemented yet.

## Hardware is authoritative and must be preserved

- Use `hardware/breadboard-prototype/README.md`, `wiring.csv`, and the schematic's
  exported `netlist-summary.json` as the wiring references. Identify discrepancies
  with requirements or manufacturer documents before changing pin assignments.
- Do not modify hardware files as part of firmware work. If hardware edits are
  explicitly requested, use the Konnect workflow for KiCad changes; never patch
  KiCad source files with text manipulation. Prefer existing exported netlists
  for connectivity inspection. Preserve the original exports and line endings.
- Target ESP32-S3-DevKitC-1-N32R16V: 32 MB octal flash and 16 MB octal PSRAM;
  memory operates at 1.8 V, external GPIO logic at 3.3 V. Keep the verified memory
  configuration; do not substitute generic ESP32-S3 defaults or burn eFuses.
- I2C SDA/SCL: GPIO8/9. Start at 100 kHz. OLED SH1107 reset: GPIO10, expected
  address 0x3C. PCF8523: 0x68; BH1750: 0x23; seesaw encoder: 0x36, polled.
- MAX98357A BCLK/LRCLK/DIN: GPIO5/6/7. GAIN is tied to VIN, SD is not connected
  to the MCU. Do not implement or claim software amplifier shutdown.
- Buttons: GPIO11/12 to ground with internal pull-ups. Battery ADC: GPIO1 through
  100k/100k and 100nF; maximum input 2.1 V. Report voltage, not an uncalibrated
  percentage. The divider measures the battery, not charger LOAD OUT.
- PN7160 IRQ/VEN: GPIO15/16, corresponding to mikroBUS INT/PWM. RST is ClickID,
  not PN7160 reset. Verify the I2C variant, 3.3 V selection and address straps on
  arrival. Both address straps zero imply 7-bit 0x28; supported range 0x28–0x2B.
  PN532 drivers are not interchangeable with PN7160 NCI drivers.
- Flash instructions must include the wiring guide's power rule: disconnect U3
  OUT from U1 5V before connecting programming USB; remove USB before reconnecting
  the boost supply. Do not assume this physical preparation has been performed.

## Architecture and behavioral invariants

- Follow the separate module responsibilities in `docs/architecture.md`: portable
  alarm core, timekeeping/RTC, configuration, audio, shared I2C, display/controls,
  NFC, web/Wi-Fi and sensing. Keep `main` focused on integration.
- Keep scheduling/state logic testable on the host without ESP-IDF or hardware.
  Give alarm state one owner and use bounded event queues across tasks. Serialize
  shared I2C access, use finite transaction deadlines and backoff, and do not hold
  the bus while waiting for NFC IRQ or other long operations.
- Audio and alarm operation must not block on web requests, Wi-Fi, NFC or missing
  peripherals. Report missing devices explicitly and keep unrelated features usable.
- An active alarm continues until an enrolled tag event dismisses it. Unknown
  tags, ordinary controls and web/configuration changes must not bypass dismissal.
  Implement a controlled volume ramp and a bounded, explicit idle-only audio test.
- Simulated NFC must use the same authorization/state path as physical NFC,
  require both `CLOCK_DEVELOPMENT_BUILD` and `CLOCK_SIMULATED_NFC`, and be absent
  or inaccessible in production. Show conspicuous development indicators in the
  web and display UIs when those UIs exist. A banner alone is not a simulated reader.
- Before implementing or changing reboot recovery, missed alarms, overlaps or DST
  handling, describe and update their policies in `docs/architecture.md`. Preserve
  active occurrences across reboot; prevent duplicate occurrence triggering. Do
  not silently discard active state on storage faults or configuration changes.
- Keep RTC time in UTC and distinguish valid time from uninitialized/stale time.
  Support manual time and optional network synchronization; normal operation must
  remain independent of Wi-Fi and internet once configured.
- Use schema-versioned, validated, bounded persistent settings and enrolled tags.
  Avoid unnecessary flash writes and never silently erase settings on a load error.
- Serve a small local HTML/CSS/JavaScript UI with no external assets or CDN.
  Require deliberate Wi-Fi setup and protected configuration access; do not log
  credentials or add unauthenticated configuration/dismissal endpoints.
- Do not call the clock tamper-proof or tag identifiers cryptographic authentication.

## Build and dependency workflow

ESP-IDF is pinned to **v6.1**, commit
`fff9895c82d744c7237be8847347bdd1b07c6643`. No third-party components or Arduino
are currently used. Verify real APIs against dependency sources/documentation;
pin any added dependency and compile it. Arduino is acceptable only with a clear
benefit and verified compatibility. Do not upgrade the SDK incidentally.

Use an ESP-IDF terminal. On this workstation:

```bash
source /home/tanner/.espressif/tools/activate_idf_v6.1.sh
idf.py --version
```

Reference builds and checks, run from the repository root:

```bash
idf.py -B build/m1 -D SDKCONFIG=build/m1/sdkconfig build
python3 tools/check_build.py build/m1
idf.py -B build/m1-dev -D SDKCONFIG=build/m1-dev/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.development' build
python3 tools/check_build.py build/m1-dev --development
cmake -S tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
git -c core.whitespace=cr-at-eol diff --check
```

Keep development and production configurations separate. The old root `sdkconfig`
was generated for ESP32 / 2 MB and must not be reused. Generated sdkconfig values
override defaults: verify the effective configuration after changing defaults.
If build paths change for a later milestone, update the README, VS Code settings,
checks and these instructions together. Do not commit build outputs, logs, secrets
or KiCad lock files. Preserve unrelated user changes and do not commit or push
unless requested.

## Incremental delivery and validation

Proceed in the requested order: (1) build/diagnostics, (2) peripheral bring-up and
low-volume audio test, (3) alarm core with simulated NFC, (4) persistence and local
web configuration, (5) physical PN7160 integration. The PN7160 is now connected,
but do not skip Milestone 4's durable state/configuration work before enabling
production alarms or physical dismissal.

Compile and test each meaningful increment. Build both profiles when shared code,
configuration or development gates change. Add meaningful host tests as the
corresponding features arrive: scheduling, weekdays, timezone/DST transitions,
reboot recovery, missed/overlapping occurrences, settings validation, valid/invalid
tag dismissal and attempts to bypass dismissal. Test storage failures and duplicate
events where they affect recovery. Documentation-only edits need no firmware rebuild.

For each milestone update the README and validation record with what works, exact
build/flash/monitor commands, manual test steps and expected results, limitations,
and outstanding hardware checks. Distinguish compiled, host-tested, simulated,
user-reported hardware-tested and agent-observed hardware-tested behavior. Never
claim flashing or physical verification without evidence. Keep earlier validation
results scoped to the version and feature actually tested.

If multiple agents are used, assign distinct module/file ownership and designate
one integration owner for shared interfaces and the final build. Avoid concurrent
edits to shared build/configuration files; do not revert another agent's work.

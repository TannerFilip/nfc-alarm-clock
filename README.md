# NFC-dismiss alarm clock firmware

Milestone 1: C++ ESP-IDF boot and memory diagnostics. **Not yet an operational alarm clock.**
No hardware has been flashed or tested by the author of this milestone.

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

This currently prints the development/simulation banner; tag injection arrives
in milestone 3. Never ship the development profile. To use the extension's
Build/Flash buttons instead, configure `idf.buildPath` to `${workspaceFolder}/build/m1`
and add `-DSDKCONFIG=build/m1/sdkconfig` to `idf.cmakeCompilerArgs`, then select
the UART port. The CLI commands above are the reference workflow.
These production build settings are already supplied in `.vscode/settings.json`.

## Milestone 1 manual acceptance

1. Confirm the board label is ESP32-S3-DevKitC-1-N32R16V and isolate the boost supply as above.
2. Flash the production profile and observe the milestone/version banner.
3. Expect flash **33554432 bytes**, PSRAM **16777216 bytes**, and `memory=OK`.
   Boot performs IDF's PSRAM memory test. Free heap is smaller than physical RAM.
4. Observe increasing uptime every 10 seconds for at least a minute. Tap RESET:
   expect another boot banner and uptime starting over, without a reset loop.
5. Expect `time INVALID`, `peripherals NOT PROBED`, and NFC not initialized.
   Peripheral absence must not prevent this diagnostic from running.
6. Production must say simulated dismissal is disabled. Development must display
   `DEVELOPMENT BUILD` and `SIMULATED NFC selected` warnings.

A memory mismatch logs a fault rather than claiming readiness. Missing PSRAM is
allowed for diagnostics, but is a failed acceptance check for this board. A
bootloader/flash configuration failure can prevent application logging entirely.
OLED, RTC, I2C discovery, buttons, encoder, battery, audio and Wi-Fi are **not
initialized in this milestone**. Silence is not software amplifier shutdown;
the amplifier SD pin is not connected to the MCU.

See [architecture and policies](docs/architecture.md),
[hardware evidence](docs/hardware.md), [validation](docs/validation.md), and
[future features](docs/backlog.md).

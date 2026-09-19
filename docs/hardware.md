# Hardware evidence and unresolved checks

Inspected 2026-09-19 before firmware edits. The workspace initially had only an empty IDF C
starter, ESP32/2 MB generated configuration, VS Code and container files; no wiring
guide, schematic or project README. No AGENTS.md was found in the workspace.
The user subsequently supplied `hardware/breadboard-prototype/`, which is now
the authoritative reference. Read its README, wiring.csv and exported
netlist-summary.json. The wiring guide and exported netlist agree with the GPIO
table, amplifier connections, NFC pinout and battery divider in the request.
No hardware files were modified. The hardware README's old project filename
`NFC_Alarm_Prototype.kicad_pro` is stale; the supplied file is named
`breadboard-prototype.kicad_pro`. This is a documentation naming issue only.

Located `/home/tanner/Documents/NFC_Alarm_Prototype.zip`; read its existing
`netlist-summary.json`, not edited schematic source. U1 matches every requested
GPIO. U4 confirms GAIN and VIN both on VCC and SD unconnected; R1/R2 are 100k and
C2 is 100nF. U9 confirms PWM/VEN and INT/IRQ. The separate
`/home/tanner/Alarm-Clock/Alarm-Clock.kicad_sch` exported an empty netlist, so it
cannot establish wiring. All original hardware files/archive remain untouched.
The supplied hardware directory supersedes that initial archive lookup.
No firmware peripheral pins are driven in milestone 1.

| Function | GPIO / address from request, corroborated GPIO netlist |
| --- | --- |
| I2C SDA/SCL | 8 / 9 |
| SH1107 reset | 10; expected 7-bit address 0x3C |
| PCF8523 | expected 0x68 |
| BH1750 | expected 0x23 |
| seesaw encoder | expected 0x36; INT not connected, poll |
| MAX98357A BCLK/LRCLK/DIN | 5 / 6 / 7; no MCU SD pin |
| Buttons to ground | 11 / 12, internal pull-ups |
| Battery ADC | 1, 100k/100k and 100nF, 2.1 V max input |
| PN7160 IRQ/VEN | 15 / 16; mikroBUS INT / PWM |

No GPIO discrepancy was found against the prototype exported netlist. Peripheral
addresses above are expectations, not device identities established by probing.

## Manufacturer checks

- [Espressif board table](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html):
  N32R16V is WROOM-2 with 32 MB octal flash and 16 MB octal PSRAM, 1.8 V memory
  interface. This is not the external 3.3 V peripheral logic voltage.
- [Memory configuration](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/flash_psram_config.html):
  use octal flash DTR 80 MHz and octal PSRAM 80 MHz, with PSRAM autodetection and
  boot memory test. Firmware compares detected capacities. IDF's image header may
  say `dout`: its OPI configuration deliberately encodes that boot header value;
  `CONFIG_ESPTOOLPY_OCT_FLASH` and `FLASHMODE_OPI` select actual octal operation.
  Factory memory voltage/eFuses are not modified.
- [ESP-IDF v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1):
  stable release matching the installed SDK; use native IDF APIs, no Arduino
  compatibility dependency. API declarations checked against the installed SDK.
- [MIKROE NFC 7 I2C](https://www.mikroe.com/nfc-7-click-i2c): I2C variant
  PN7160A1HN/C100, VCC SEL must be 3.3 V; ADDR SEL jumpers default both 0 and
  I2C interface resistors must be populated. PWM is VEN; INT is IRQ; RST belongs
  to ClickID. [NXP UM11495](https://www.nxp.com/docs/en/user-manual/UM11495.pdf)
  specifies 7-bit addresses 0x28–0x2B. Both address straps 0 imply **0x28**;
  0x50/0x51 are write/read address bytes, not 7-bit driver addresses. Inspect
  the delivered board's straps and confirm NCI communication before selecting
  its final address. No address or PN532 substitute driver is implemented yet.

Before milestone 2: confirm board marking, actual I2C pull-ups and
addresses, OLED reset routing, and power arrangement. Before battery tests,
confirm the actual battery/power-path assembly and measure voltage at GPIO1.

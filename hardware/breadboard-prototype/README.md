# NFC alarm clock — breadboard prototype

Open `NFC_Alarm_Prototype.kicad_pro` in KiCad, then its schematic. The PDF is a printable A3 wiring drawing. `wiring.csv` lists every represented terminal. Keep `Prototype.kicad_sym` and `sym-lib-table` alongside the project.

This is a functional module wiring diagram. Module pin identifiers follow the board labels, not the underlying IC pin numbers or physical header order. Unused headers and onboard circuitry are omitted. The PCB file is blank; this project is not ready for fabrication.

## Parts represented

| Reference | Part |
|---|---|
| U1 | Espressif ESP32-S3-DevKitC-1-N32R16V |
| U2 | Adafruit 4755 BQ24074 charger with power path and USB-C |
| U3 | Adafruit 4654 TPS61023 MiniBoost, nominal output 5.2 V |
| BT1 | Protected 1S LiPo, 3.7 V nominal / 4.2 V full, 1200 mAh; Adafruit 258 proposed |
| U4 | Adafruit 3006 MAX98357A I2S amplifier |
| LS1 | Same Sky CMS-4012-34L200-X7, 4 ohm / 3 W speaker |
| C1 | Panasonic EEU-FR1E221B, 220 uF / 25 V polarized capacitor |
| U5 | Adafruit 4650 128x64 SH1107 OLED FeatherWing |
| U6 | Adafruit 5189 PCF8523 RTC breakout; separate CR1220 in its onboard holder |
| U7 | Adafruit 4681 BH1750 light sensor |
| U8 | Adafruit 5880 seesaw rotary encoder with push switch |
| U9 | Proposed MikroE MIKROE-6453 NFC 7 Click I2C, PN7160 |
| SW1, SW2 | Omron B3F-1020 push buttons |
| R1, R2 | 100 kohm each |
| C2 | 100 nF ceramic |

U9 is a proposed selection, not a confirmed substitution from the shopping cart. This pinout applies specifically to the NFC 7 Click **I2C** board. Its mikroBUS pin 16 (PWM) controls VEN, and pin 15 (INT) carries IRQ. Pin 2 (RST) belongs to ClickID and is not PN7160 reset. Use the board's 3.3 V selection, leave its 5 V terminal unused, and check its address straps against the firmware.

## Power and assembly

- Plug a suitable 5 V USB supply into U2's own USB-C connector. A separate USB-C breakout is unnecessary here.
- Connect the protected 1S battery to U2's battery connector, checking polarity. Set the charger to 500 mA using its documented charge-current jumper configuration; do not leave a higher setting without checking the battery's charge rating.
- `VBUS` is USB input. `+BATT` is the battery terminal. `VCC` is the charger LOAD OUT, approximately 3–4.4 V. These are separate nets.
- Feed U3 IN from LOAD OUT and tie EN to IN. `+5V` denotes U3's nominal 5.2 V output, feeding U1's 5V terminal. U1's regulator supplies the `+3V3` rail.
- Before attaching a programming USB cable to U1, disconnect U3 OUT from U1 5V. Reconnect after removing that cable. Follow the DevKit's power-source restrictions.
- Feed the amplifier from LOAD OUT. Fit C1 near its VIN/GND, observing capacitor polarity. Tie GAIN to VIN for 6 dB; leave SD open to use this breakout's default mono mix. Neither speaker lead connects to ground.
- All grounds are common. Keep the NFC antenna away from metal, the speaker, and switching-power wiring. Keep speaker and supply jumpers short. Existing module decoupling is not repeated in the diagram.
- The modules supply I2C pull-ups. Start at 100 kHz with short wiring; inspect the combined pull-up resistance and signal edges if communication is unreliable. All bus logic is 3.3 V.
- Check the combined 3.3 V load and regulator temperature with Wi-Fi transmitting and the NFC field active. This schematic does not prove current margin, RF performance, or runtime; those need a powered prototype test.

## Firmware pin map

| GPIO | Function |
|---|---|
| 1 | Battery ADC through R1/R2; maximum 2.1 V at full charge |
| 5 | I2S BCLK |
| 6 | I2S LRCLK / LRC |
| 7 | I2S data to amplifier |
| 8 | Shared I2C SDA |
| 9 | Shared I2C SCL |
| 10 | OLED reset |
| 11 | SW1, internal pull-up |
| 12 | SW2, internal pull-up |
| 15 | PN7160 IRQ input |
| 16 | PN7160 VEN output |

Configure the N32R16V's octal flash and PSRAM correctly. The GPIO interface is still 3.3 V. Configure ADC attenuation for the divider range and calibrate readings; the divider measures the battery, not LOAD OUT.

The OLED uses an SH1107 driver (Adafruit SH110X), default address 0x3C, and a startup reset pulse. Its extra buttons are unused. PCF8523 uses 0x68, BH1750 0x23, and seesaw 0x36. Poll the encoder's push switch through seesaw. The basic prototype keeps the MCU awake and handles alarm timing in software using the RTC; SQW and encoder INT remain open. RTC-triggered deep sleep is not implemented by this wiring. PN7160 requires a compatible NCI software stack, not a PN532 driver.

## Validation

KiCad ERC completed with **0 errors and 0 warnings**. Konnect found no floating wire ends, unconnected represented pins, orphan items, shorted named nets, or overlapping symbol geometry. Every intended signal and supply connection was compared with the exported netlist, including the battery divider and isolated speaker outputs. The drawing was rendered and visually reviewed. These checks validate the represented wiring, not module internals or physical hardware performance.

## Manufacturer references

- [ESP32-S3 DevKitC-1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide.html)
- [Adafruit BQ24074 charger](https://www.adafruit.com/product/4755)
- [Adafruit MiniBoost](https://www.adafruit.com/product/4654)
- [MAX98357A breakout pinouts](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)
- [OLED FeatherWing](https://www.adafruit.com/product/4650)
- [PCF8523 breakout](https://www.adafruit.com/product/5189)
- [BH1750 breakout](https://www.adafruit.com/product/4681)
- [Seesaw encoder](https://www.adafruit.com/product/5880)
- [NFC 7 Click I2C](https://www.mikroe.com/nfc-7-click-i2c)

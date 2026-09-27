# Milestone 3: alarm core and simulated NFC

Firmware version 0.3.0. The portable alarm core is integrated through a dedicated
owner task and bounded queues. Both profiles compile against pinned ESP-IDF v6.1;
the host suite validates policy logic without ESP-IDF or hardware.

## Implemented behavior

- Up to eight weekday schedules and eight active overlaps in the portable core.
  Occurrences use stable alarm-ID/local-date keys and a UTC due time.
- UTC and `America/Los_Angeles` scheduling, including US DST rules before and
  after 2007. Spring-gap alarms are skipped and only the first fall-fold instance
  fires. Weekday masks use the local calendar.
- First valid-time acquisition does not ring retroactively. Later interruptions
  catch up at most five minutes; older occurrences increment a saturating missed
  counter without ringing. Backward clock changes do not replay occurrences.
- The journal contract persists activation before requesting audio and persists
  authorized dismissal before requesting silence. Failed activation writes do
  not start sound; failed dismissal writes leave the alarm active. Cursor-only
  progress checkpoints every five minutes, while state changes commit immediately.
- Active occurrences and their frozen enrolled-tag snapshot can be restored by a
  durable journal. The host suite covers recovery, corrupt loads and injected
  save failures. Milestone 3's device adapter is intentionally RAM-only; NVS is
  Milestone 4 work.
- Corrupt or failed journal loads block mutation/evaluation writes, preventing a
  fault from being silently treated as empty or overwritten.
- One audio stream serves overlapping occurrences. It ramps a 440 Hz tone from
  1% to the user-verified 5% digital peak over 30 seconds, then holds 5% until an
  authorized dismissal.
- The development profile exposes RAM-only setup, immediate trigger and `nfc_sim`.
  Simulated tags enter the same `handle_tag` authorization method reserved for a
  future physical reader. Unknown tags and ordinary controls never dismiss.
- The production console contains no simulator/setup/trigger commands, and the
  checked simulator markers are absent from its ELF. With no durable configuration
  yet, production alarm mutation remains disabled.

## Development console

| Command | Behavior |
| --- | --- |
| `alarm_status` | Report owner state, active/missed counts, zone and RAM-only persistence |
| `tag_enroll HEX_UID` | Development-only RAM enrollment; 1-10 UID bytes |
| `alarm_zone UTC` | Select UTC for future evaluation |
| `alarm_zone America/Los_Angeles` | Select tested Pacific rules for future evaluation |
| `alarm_set HH:MM SMTWTFS` | Development-only alarm ID 1; seven `0`/`1` weekday bits |
| `alarm_trigger` | Development-only immediate occurrence; requires valid time and enrolled tag |
| `nfc_sim HEX_UID` | Development-only tag event through the normal authorization path |

`tag_enroll`, `alarm_zone`, `alarm_set`, `alarm_trigger` and `nfc_sim` require both
`CLOCK_DEVELOPMENT_BUILD` and `CLOCK_SIMULATED_NFC` through the Kconfig dependency
and compile-time check. They are absent from the production console and artifact.
The OLED displays `DEV SIM NFC - RAM ONLY` whenever that profile is running.

While ringing, time setting, audio tests, schedule/timezone changes, enrollment
and another trigger are rejected. Buttons and encoder operations remain available
for diagnostics/brightness but are not dismissal events. Tag identifiers are not
cryptographic authentication, and this device is not tamper-proof.

## Physical NFC boundary

The user reports the PN7160 board is connected. Milestone 3 does not configure
GPIO15 IRQ, GPIO16 VEN, register an I2C transport, or send NCI packets. The scan
labels ACKs at 0x28-0x2B as possible PN7160 presence only. A missing ACK does not
yet diagnose the board because VEN sequencing is not implemented. Before the
physical integration milestone, confirm the I2C board variant, 3.3 V selection,
ADDR strap state, and expected VEN/IRQ behavior.

## Remaining limitations

- Reset loses the development alarm, tags, timezone and RAM journal. Do not use
  this profile as an alarm clock. Durable schema-versioned settings and journal
  integration arrive in Milestone 4.
- No web UI, Wi-Fi setup, physical PN7160 reader, enrollment mode, or production
  configuration path exists yet.
- The user reports the device alarm sounded and was dismissed through the enrolled
  simulated-tag path. This was not agent-observed and does not validate physical NFC;
  the remaining bypass, unknown-tag and schedule cases have not been reported.
- Existing RTC retention, calibrated battery accuracy and peripheral fault tests
  from Milestone 2 remain open.

Use the root README for the mandatory boost/USB power isolation rule, exact build
and flash commands, and the manual simulator acceptance sequence.

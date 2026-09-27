# Milestone 4: durable settings and local setup

Firmware 0.4.0 adds the first complete persistence and local-configuration path.
It is compiled and host-tested but has not yet been flashed or exercised on the
physical clock for this milestone.

## Implemented

- A portable, fixed 195-byte settings record with schema, canonical validation,
  deterministic little-endian encoding and CRC32. It bounds eight alarms, eight
  tags, two tested timezones, device name, brightness, ambient mode and 1-5%
  maximum alarm volume.
- Separate NVS namespaces for settings and the alarm occurrence journal. The
  journal has its own magic, version, length and CRC. Writes compare the existing
  value first and commit only changed data.
- NVS initialization never auto-erases on full, corrupt or newer-format storage.
  Load/schema/checksum failures are visible faults and do not silently install or
  persist defaults.
- Settings load before alarm journal recovery. The alarm core receives one atomic
  configuration snapshot; accepted mutations are persisted before being applied.
- The audio ramp now captures the validated 1-5% configured maximum when ringing
  starts. Later configuration cannot lower an active alarm's captured volume.
- A three-second Button 2 hold while idle starts a 15-minute WPA2 SoftAP. Each
  start generates a new SSID and 12-character AP password from an OLED-friendly
  alphabet. The OLED initially shows a standard Wi-Fi join QR code plus the setup
  URL; Button 1 selects a text credential fallback. The QR uses medium error correction, a four-module quiet
  zone and Espressif's `qrcode` component pinned to 0.2.0. Credentials appear
  only on the OLED and are neither logged nor stored by the Wi-Fi driver.
- A self-contained local page at `http://192.168.4.1/` configures timezone,
  alarms, enrolled tag identifiers, display behavior, maximum volume, device name
  and manual UTC. It has no CDN or external assets.
- The web path bounds bodies, sockets, clients and request rate. Mutation
  responses acknowledge queue admission; durable acceptance is reported by clock
  status/logs. Access requires the WPA2 password; HTTP mutations retain a
  per-start CSRF token plus exact Host and Origin checks. Configuration requests
  are rejected while ringing. The AP is stopped
  if an alarm begins; alarm/audio tasks never wait for Wi-Fi.

## Security boundary

This increment uses HTTP only inside the temporary WPA2 SoftAP; it does not claim
transport confidentiality beyond that local radio link. CSRF and origin checks supplement WPA2 access control, but HTTPS with a locally
trusted certificate is not implemented. There is no STA/LAN listener, internet dependency, cloud service or
stored AP credential. Tag identifiers are still identifiers, not cryptographic
authentication.

Production deliberately rejects enabled alarm schedules until the physical
PN7160 dismissal path exists. If a production image finds enabled schedules left
by a development image, it suppresses them without erasing the NVS record. The
development profile remains the only profile suitable for alarm testing through
`nfc_sim`.

## Manual acceptance

Follow the README power-isolation rule, flash the development profile and verify
the `milestone 4 / 0.4.0` banner and `NVS initialization: READY`.

1. While idle, hold Button 2 for three seconds. Confirm the OLED shows a Wi-Fi QR, setup URL and remaining time. Press
   Button 1 and confirm the unique SSID and 12-character WPA2 password appear as text; press again for QR.
2. Scan the QR on a separate device and confirm it offers to join the clock AP.
   Browse to `http://192.168.4.1/`; confirm configuration opens directly,
   then save a tag before enabling an alarm.
3. Configure timezone, alarm, brightness/ambient behavior, maximum volume, device
   name and current UTC. Invalid/out-of-range requests must be rejected.
4. Reboot normally. Confirm `alarm_status` reports `NVS SETTINGS + JOURNAL` and the
   saved schedule/tag/timezone/display/volume behavior remains.
5. Start setup again and confirm the SSID and password changed. Wait for expiry or let
   an alarm start; the AP must stop without stopping or delaying the alarm.
6. While ringing, confirm setup cannot start and configuration requests are
   rejected. Dismiss with the development enrolled `nfc_sim` identifier.
7. Interrupt power after an alarm has durably started, reboot, and confirm it
   resumes. Dismiss it, reboot again, and confirm it does not resurrect.

## Remaining limitations

- The user reports that the current QR join, WPA2 connection and direct browser
  configuration page work on the physical device. This was not agent-observed.
- Device-side NVS failure injection, reboot recovery, AP expiry, configuration
  persistence and disconnection-during-alarm are not yet physically observed.
- There is no deliberate storage-repair/reset UI; corrupt/newer settings remain
  blocked and preserved for diagnosis rather than erased.
- No STA Wi-Fi, SNTP synchronization or HTTPS certificate workflow exists yet.
- Physical PN7160 NCI transport and RF tag enrollment/dismissal remain Milestone 5.

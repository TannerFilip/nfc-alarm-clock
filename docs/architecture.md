# Architecture and incremental delivery

Milestones 1-3 are implemented and Milestone 4 persistence/local setup is in progress.
Milestone 2 has partial user-reported physical
verification, including audible output at 5%; remaining acceptance is documented
in `docs/validation.md`.
`main/drivers/` contains I2C, RTC, controls, display, sensing and audio bring-up.
`main/core/` contains portable calendar, alarm, scheduling, tag and control helpers.
`alarm_service.cpp` owns alarm state on a dedicated task; `bringup.cpp` integrates
bounded command events. Validated NVS and a time-limited SoftAP/web UI are integrated; physical NFC remains planned.

| Module | Responsibility |
| --- | --- |
| `alarm_core` | Hardware-independent C++ state machine; schedule occurrences, active set, tag authorization; no IDF calls |
| `timekeeping` | UTC system clock, PCF8523 UTC backing, explicit validity, POSIX timezone conversion, manual setting, optional SNTP |
| `configuration` | Validated versioned settings and enrolled tags in NVS; atomic durable alarm journal; no writes per tick |
| `audio` | Dedicated I2S DMA task, monotonic volume ramp and bounded idle-only audio test; no SD control |
| `i2c_bus` | Single owner/serialized transactions with finite deadlines; peripheral failures and recovery backoff |
| `display_controls` | SH1107 rendering, seesaw polling and debounced buttons; emits intent events |
| `nfc` | Reader interface emits bounded tag IDs/status; production PN7160 NCI transport; separate development simulator |
| `web_wifi` | Local HTML/CSS/JS assets, validated configuration requests, deliberate time-limited SoftAP; no STA/LAN listener yet |
| `sensing` | BH1750 lux/dimming and calibrated ADC voltage; divider factor 2, no invented battery percentage |
| `main` | Integration, bounded queues and status; milestone 4 persistence and setup integration today |

The alarm task owns state. Hardware, NFC and web tasks submit typed events through
bounded queues. Scheduling and audio never wait for network or NFC. I2C requests
have deadlines (initial target 20 ms per transfer), with IRQ/NFC waits outside
the bus lock. Time-critical state and DMA buffers use internal RAM. Missing
peripherals are explicit status faults, not global startup failures. No confirmed
RTC time means unsynchronized status, not a fabricated current time.

## Alarm policies implemented in the Milestone 3 core

The portable core implements these rules behind an abstract journal. Host tests
exercise recovery and injected journal failures. Firmware 0.4.0 connects the same interface to a checksum-protected NVS journal
and loads a separate validated settings record before journal recovery. Development
simulation commands remain compiled out of production.

- States: `time_invalid`, `idle`, `ringing`, plus orthogonal device/storage faults.
  A restored active occurrence rings even when wall-clock time is invalid.
- Each occurrence has a stable alarm ID and local calendar date, stored with its
  UTC due time. Persist the active set before starting sound. Record dismissal
  durably before silence; failed persistence keeps ringing and reports a fault.
  Power loss between durable dismissal and audio stop must not resurrect it.
- On reboot, resume every persisted active occurrence with a short volume ramp.
  Never infer dismissal from boot, time changes or configuration edits. Journal
  corruption is a visible recovery fault, never silently treated as empty.
- Missed alarms: when valid time resumes, catch up occurrences at most 5 minutes
  late; record older ones as missed for status, without ringing. First setup does
  not retroactively ring. The journal retains a UTC evaluation cursor, a saturating
  missed counter and the most recent 32 occurrence keys FIFO; the cursor prevents
  replay after older history expires or backward clock adjustments. Occurrence,
  dismissal, missed and skipped changes commit immediately; an otherwise-idle
  cursor checkpoints at most once per five minutes rather than once per tick.
- Overlaps join one active set and one audio stream. One enrolled tag scan
  dismisses all occurrences active at that instant. A later occurrence is new.
- DST spring gap: skip the nonexistent local time and report it as skipped. DST
  fall fold: fire only the first occurrence of the selected local time that date.
  Weekdays refer to the configured local calendar, not UTC.
- Milestone 3's curated zones are UTC and `America/Los_Angeles`. The latter uses
  the applicable US DST transition rules for 2000-2099, including the 2007 rule
  change. Additional curated zones require their own transition tests.
- Timezone/manual-time edits affect future scheduling only. Forward changes use
  the missed-alarm window; backward changes never replay recorded occurrences.
  Freeze each ringing occurrence and its enrolled-tag authorization snapshot.
- While ringing, buttons, encoder and configuration cannot dismiss, disable the
  audio path, lower its captured maximum volume, delete its authorization snapshot,
  or initiate a reset/setup/audio-test operation that bypasses dismissal. Reject
  incompatible writes with a visible error; do not silently accept and ignore them.
- Production will accept only physical enrolled tag events once that reader exists;
  it currently exposes no tag path. Development injection uses
  the same authorization/state path, requires both build flags, and will carry
  conspicuous OLED/web badges. Unknown tags never dismiss. Enrollment is allowed
  only while idle through authenticated, time-limited explicit enrollment mode.

Tag identifiers are not cryptographic authentication. Removing power stops the
hardware; the clock is not tamper-proof. Battery duration remains a physical test.

## Configuration and access implementation (milestone 4)

Firmware 0.4.0 uses schema-versioned, size-bounded NVS records with validation for names, alarm count,
weekday masks, times, timezone syntax, tag lengths/count, volume and brightness.
Reject unsupported schema versions without erasing data. Write only changed
settings; separate settings from the low-frequency occurrence journal.

Keep RTC in UTC; expose explicit valid/invalid/stale status and synchronization
source. Use a curated timezone list mapping to tested POSIX TZ rules (IDF does
not ship a full IANA zone database). Manual setting remains available offline.

A three-second Button 2 hold while idle opens a 15-minute WPA2 SoftAP with
per-start credentials displayed only on the OLED as a Wi-Fi join QR with a
Button 1 text fallback. The QR dependency is exact-pinned, and its
credential-bearing payload is encoded with the component log tag temporarily
suppressed. The local HTTP UI is confined to that AP and uses bounded requests
and rate, a per-start CSRF token, and exact Host/Origin checks. The WPA2 password
is the only user-entered setup credential. There is no LAN listener or external
asset.
HTTPS and STA provisioning remain future work. Wi-Fi shutdown never changes alarm
state, and setup stops if an alarm begins.

## Delivery gates

1. **Built; user reports hardware working:** pinned build, memory/boot logging, architecture, flash instructions.
2. **Built; partially user-tested:** Shared I2C discovery and fault status; OLED, RTC, seesaw, buttons; explicit
   bounded low-volume I2S test; voltage and light sensing. Validate each on-device.
3. **Built, host-tested and user-tested:** Portable core and simulator: host tests for scheduling, weekdays, DST gaps/folds,
   forward/backward clock changes, recovery, overlap, unknown/valid tags and bypass
   attempts. Fault-inject journal writes. Compile both development and production.
4. **Current, built and host-tested; device validation pending:** deterministic settings validation/codec, checksum-protected NVS settings and journal, and bounded WPA2-protected local setup UI. Exercise reboot recovery, access control, bad requests, expiry and disconnection during alarm on-device.
5. PN7160 NCI integration (board now connected but unverified): verify straps/address/voltage,
   controller reset/init, tag discovery/enrollment and disconnected-reader recovery.

Every gate gets its own recorded build/test results and exact flash/manual tests.
Successful compilation never marks a hardware gate passed.

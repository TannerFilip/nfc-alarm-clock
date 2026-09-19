# Architecture and incremental delivery

Only milestone 1 is implemented. Module boundaries below are the plan for the
next increments; empty driver implementations are deliberately not provided.

| Module | Responsibility |
| --- | --- |
| `alarm_core` | Hardware-independent C++ state machine; schedule occurrences, active set, tag authorization; no IDF calls |
| `timekeeping` | UTC system clock, PCF8523 UTC backing, explicit validity, POSIX timezone conversion, manual setting, optional SNTP |
| `configuration` | Validated versioned settings and enrolled tags in NVS; atomic durable alarm journal; no writes per tick |
| `audio` | Dedicated I2S DMA task, monotonic volume ramp and bounded idle-only audio test; no SD control |
| `i2c_bus` | Single owner/serialized transactions with finite deadlines; peripheral failures and recovery backoff |
| `display_controls` | SH1107 rendering, seesaw polling and debounced buttons; emits intent events |
| `nfc` | Reader interface emits bounded tag IDs/status; production PN7160 NCI transport; separate development simulator |
| `web_wifi` | Local HTML/CSS/JS assets, validated configuration requests, deliberate setup mode and optional STA connection |
| `sensing` | BH1750 lux/dimming and calibrated ADC voltage; divider factor 2, no invented battery percentage |
| `main` | Integration, queues and status; milestone 1 boot diagnostics today |

The alarm task owns state. Hardware, NFC and web tasks submit typed events through
bounded queues. Scheduling and audio never wait for network or NFC. I2C requests
have deadlines (initial target 20 ms per transfer), with IRQ/NFC waits outside
the bus lock. Time-critical state and DMA buffers use internal RAM. Missing
peripherals are explicit status faults, not global startup failures. No confirmed
RTC time means unsynchronized status, not a fabricated current time.

## Proposed policies, chosen for implementation in milestone 3

These decisions are documented before implementation and can still be revised.

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
  not retroactively ring. Maintain a durable evaluation cursor/occurrence history
  to avoid replay after reboot or backward clock adjustments; bounded history
  and retention rules must be specified/tested before implementation.
- Overlaps join one active set and one audio stream. One enrolled tag scan
  dismisses all occurrences active at that instant. A later occurrence is new.
- DST spring gap: skip the nonexistent local time and report it as skipped. DST
  fall fold: fire only the first occurrence of the selected local time that date.
  Weekdays refer to the configured local calendar, not UTC.
- Timezone/manual-time edits affect future scheduling only. Forward changes use
  the missed-alarm window; backward changes never replay recorded occurrences.
  Freeze each ringing occurrence and its enrolled-tag authorization snapshot.
- While ringing, buttons, encoder and configuration cannot dismiss, disable the
  audio path, lower its captured maximum volume, delete its authorization snapshot,
  or initiate a reset/setup/audio-test operation that bypasses dismissal. Reject
  incompatible writes with a visible error; do not silently accept and ignore them.
- Production accepts only physical enrolled tag events. Development injection uses
  the same authorization/state path, requires both build flags, and will carry
  conspicuous OLED/web badges. Unknown tags never dismiss. Enrollment is allowed
  only while idle through authenticated, time-limited explicit enrollment mode.

Tag identifiers are not cryptographic authentication. Removing power stops the
hardware; the clock is not tamper-proof. Battery duration remains a physical test.

## Configuration and access plan (milestone 4)

Schema-versioned, size-bounded NVS records with validation for names, alarm count,
weekday masks, times, timezone syntax, tag lengths/count, volume and brightness.
Reject unsupported schema versions without erasing data. Write only changed
settings; separate settings from the low-frequency occurrence journal.

Keep RTC in UTC; expose explicit valid/invalid/stale status and synchronization
source. Use a curated timezone list mapping to tested POSIX TZ rules (IDF does
not ship a full IANA zone database). Manual setting remains available offline.

Deliberate long-button setup while idle opens a time-limited WPA2 SoftAP with a
random per-device credential displayed locally. No default shared password or
credential logging. Configuration needs authentication, request size/rate bounds,
CSRF/origin checks and session expiry on both AP and LAN; prefer HTTPS and explain
local certificate trust at setup. Never expose an unauthenticated LAN API. Normal
operation needs no AP, internet, CDN or external server. Wi-Fi disconnects do not
change alarm state. Provisioning reset must preserve an active alarm journal.

## Delivery gates

1. **Current:** pinned build, memory/boot logging, architecture, flash instructions.
2. Shared I2C discovery and fault status; OLED, RTC, seesaw, buttons; explicit
   bounded low-volume I2S test; voltage and light sensing. Validate each on-device.
3. Portable core and simulator: host tests for scheduling, weekdays, DST gaps/folds,
   forward/backward clock changes, recovery, overlap, unknown/valid tags and bypass
   attempts. Fault-inject journal writes. Compile both development and production.
4. NVS validation/migration tests and local web UI for every requested setting;
   exercise offline boot, access control, bad requests and disconnection during alarm.
5. PN7160 NCI integration after board arrival: verify straps/address/voltage,
   controller reset/init, tag discovery/enrollment and disconnected-reader recovery.

Every gate gets its own recorded build/test results and exact flash/manual tests.
Successful compilation never marks a hardware gate passed.
